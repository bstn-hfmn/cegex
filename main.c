#include "./util/collections.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NFA_MAX_STATES                 ARRAY_MAX_CAPACITY
#define NFA_STATE_MAX_TRANSITIONS      2
#define NFA_TRANSITION_SET_MAX_SYMBOLS 64

#define NFA_CREATE_SYMBOL(n, c, o)     nfa_create_symbol(n, ((byte[1]){ c }), 1, o)
#define NFA_CREATE_SYMBOLS(ctx, data, out) \
    nfa_create_symbol((ctx), (data), sizeof(data) / sizeof((data)[0]), (out))

#define NFA_TRANSITION_EPSILON ((nfa_transition_cond){ .type = NFA_TRANSITION_COND_EPSILON })

#define NFA_STATE_DEFAULT     \
    ((nfa_state){             \
        .is_final = false,    \
        .is_initial = false,  \
        .num_transitions = 0, \
        .transitions = { 0 } })

#define NFA_STATE_INITIAL     \
    ((nfa_state){             \
        .is_final = false,    \
        .is_initial = true,   \
        .num_transitions = 0, \
        .transitions = { 0 } })

#define NFA_STATE_FINAL       \
    ((nfa_state){             \
        .is_final = true,     \
        .is_initial = false,  \
        .num_transitions = 0, \
        .transitions = { 0 } })

#define NFA_FRAGMENT(i, f) ((nfa_fragment){ .initial = i, .final = f })

#define CEGEX_TRY(expr)        \
    do {                       \
        status = (expr);       \
        if(status != CEGEX_OK) \
            goto fail;         \
    } while(0)

typedef enum cegex_status
{
    CEGEX_OK = 0,
    CEGEX_ERR_INVALID_ARGUMENT,
    CEGEX_ERR_MEMORY,
    CEGEX_ERR_SYNTAX,
    CEGEX_ERR_LIMIT,
    CEGEX_ERR_UNSUPPORTED
} cegex_status;

typedef char byte;
typedef ARRAY(byte) byte_set;
typedef struct nfa_transition_cond
{
    enum
    {
        NFA_TRANSITION_COND_EPSILON,
        NFA_TRANSITION_COND_SET
    } type;

    byte_set set;
} nfa_transition_cond;

typedef size_t nfa_state_id;
typedef struct nfa_transition
{
    nfa_state_id to;
    nfa_state_id from;

    nfa_transition_cond condition;
} nfa_transition;

typedef struct nfa_state
{
    bool is_final;
    bool is_initial;

    nfa_state_id id;
    size_t num_transitions;
    nfa_transition transitions[NFA_STATE_MAX_TRANSITIONS];
} nfa_state;

typedef struct nfa_fragment
{
    nfa_state_id initial;
    nfa_state_id final;
} nfa_fragment;

typedef ARRAY(nfa_state) nfa_state_array;
typedef ARRAY(nfa_fragment) nfa_fragment_array;
typedef ARRAY(nfa_state_id) nfa_state_id_array;

typedef struct nfa
{
    nfa_state_array states;
} nfa;

/// @brief Initializes the NFA
/// @param ctx A valid reference to a `nfa` to initialize
/// @return `CEGEX_ERR_MEMORY` when allocation of internal array failed
cegex_status nfa_init(nfa* ctx)
{
    assert(ctx != NULL);

    ARRAY_INIT(&ctx->states);
    if(ctx->states.elements == NULL)
        return CEGEX_ERR_MEMORY;

    return CEGEX_OK;
}

void nfa_free(nfa* ctx)
{
    assert(ctx != NULL);

    for(size_t i = 0; i < ctx->states.length; i++) {
        nfa_state* state = NULL;
        ARRAY_NTH_REF(&ctx->states, i, &state);

        for(size_t j = 0; j < state->num_transitions; j++) {
            nfa_transition* transition = &state->transitions[j];
            if(transition->condition.type == NFA_TRANSITION_COND_EPSILON)
                continue;

            if(transition->condition.set.elements != NULL)
                ARRAY_FREE(&transition->condition.set);
        }
    }

    if(ctx->states.elements != NULL)
        ARRAY_FREE(&ctx->states);
}

/// @brief Creates a transition from one state to another state using the provided transition condition `cond`
/// @param ctx A initialized reference to a NFA
/// @param from The source state id of the transition
/// @param to The destination state id of the transition
/// @param cond The transition condition, used to either allow or disallow the transition from the source state to the destination state
/// @param out The created transition, nullable
/// @return `CEGEX_ERR_INVALID_ARGUMENT` if the indicies are invalid or there are no transitions available in the `from` state
cegex_status nfa_create_transition(
    nfa* ctx,
    nfa_state_id from,
    nfa_state_id to,
    nfa_transition_cond cond,
    nfa_transition** out)
{
    assert(ctx != NULL);

    // Are the state indices valid?
    if(ARRAY_EMPTY(&ctx->states) || (ctx->states.length <= from || ctx->states.length <= to))
        return CEGEX_ERR_INVALID_ARGUMENT;

    nfa_state* from_state = NULL;
    ARRAY_NTH_REF(&ctx->states, from, &from_state);

    // Are there still transitions available in the `from` state?
    if(from_state->num_transitions >= NFA_STATE_MAX_TRANSITIONS)
        return CEGEX_ERR_INVALID_ARGUMENT;

    from_state->transitions[from_state->num_transitions++] = (nfa_transition){
        .to = to,
        .from = from,
        .condition = cond
    };

    if(out != NULL)
        *out = &from_state->transitions[from_state->num_transitions - 1];

    return CEGEX_OK;
}

/// @brief Pushes a new state to the `ctx` internal array of states
/// @param ctx A initialized reference to a NFA
/// @param state The state to add
/// @param out The id of the newly added state
/// @return `CEGEX_ERR_LIMIT` when the state array is full
cegex_status nfa_create_state(
    nfa* ctx,
    nfa_state state,
    nfa_state_id* out)
{
    assert(ctx != NULL);

    if(ctx->states.length >= NFA_MAX_STATES)
        return CEGEX_ERR_LIMIT;

    state.id = ctx->states.length;
    ARRAY_PUSH(&ctx->states, state);

    if(out != NULL)
        *out = state.id;

    return CEGEX_OK;
}

/// @brief Builds two states with an epsilon transition
/// @param ctx A initialized reference to a NFA
/// @param out The output fragment
/// @return An status code: `CEGEX_OK` on success
cegex_status nfa_create_empty(nfa* ctx,
                              nfa_fragment* out)
{
    assert(ctx != NULL);
    cegex_status status = CEGEX_OK;

    nfa_state_id q0, qF;
    CEGEX_TRY(nfa_create_state(ctx, NFA_STATE_INITIAL, &q0));
    CEGEX_TRY(nfa_create_state(ctx, NFA_STATE_FINAL, &qF));

    CEGEX_TRY(nfa_create_transition(ctx, q0, qF, NFA_TRANSITION_EPSILON, NULL));

    if(out != NULL)
        *out = NFA_FRAGMENT(q0, qF);

    return status;

fail:
    nfa_free(ctx);
    return status;
}

/// @brief Builds two states with a transition and a given set of bytes as the transition condition (expands the alphabet of the NFA)
/// @param ctx A initialized reference to a NFA
/// @param bytes The set of bytes, used as transition condition
/// @param out The output fragment
/// @return An status code: `CEGEX_OK` on success
cegex_status nfa_create_symbol(
    nfa* ctx,
    byte* bytes,
    size_t n,
    nfa_fragment* out)
{
    assert(ctx != NULL);
    cegex_status status = CEGEX_OK;

    nfa_state_id q0, qF;
    CEGEX_TRY(nfa_create_state(ctx, NFA_STATE_INITIAL, &q0));
    CEGEX_TRY(nfa_create_state(ctx, NFA_STATE_FINAL, &qF));

    nfa_transition_cond cond;
    cond.type = NFA_TRANSITION_COND_SET;
    ARRAY_INIT(&cond.set);
    for(size_t i = 0; i < n; i++) {
        ARRAY_PUSH(&cond.set, bytes[i]);
    }

    CEGEX_TRY(nfa_create_transition(ctx, q0, qF, cond, NULL));

    if(out != NULL)
        *out = NFA_FRAGMENT(q0, qF);

    return status;
fail:
    nfa_free(ctx);
    return status;
}

/// @brief Build the union of `a` and `b` (`a|b`)
/// @param ctx A initialized reference to a NFA
/// @param a The left side of the union expression
/// @param b The right side of the union expression
/// @param out The output fragment, where the initial state connects to both `a` and `b`, which in turn connect to a new shared final state
/// @return An status code: `CEGEX_OK` on success
cegex_status nfa_union(
    nfa* ctx,
    nfa_fragment a,
    nfa_fragment b,
    nfa_fragment* out)
{
    assert(ctx != NULL);
    cegex_status status = CEGEX_OK;

    nfa_state_id q0, qF;
    CEGEX_TRY(nfa_create_state(ctx, NFA_STATE_INITIAL, &q0));
    CEGEX_TRY(nfa_create_state(ctx, NFA_STATE_FINAL, &qF));

    CEGEX_TRY(nfa_create_transition(ctx, q0, a.initial, NFA_TRANSITION_EPSILON, NULL));
    CEGEX_TRY(nfa_create_transition(ctx, q0, b.initial, NFA_TRANSITION_EPSILON, NULL));

    CEGEX_TRY(nfa_create_transition(ctx, a.final, qF, NFA_TRANSITION_EPSILON, NULL));
    CEGEX_TRY(nfa_create_transition(ctx, b.final, qF, NFA_TRANSITION_EPSILON, NULL));

    if(out != NULL)
        *out = NFA_FRAGMENT(q0, qF);

    return status;
fail:
    nfa_free(ctx);
    return status;
}

/// @brief Build the concatination expression of `a` and `b`
/// @param ctx A initialized reference to a NFA
/// @param a The left side of the concatination
/// @param b The right side of the concatination
/// @param out The output fragment, initial state of `a` is connected to the initial state of `b`
/// @return An status code: `CEGEX_OK` on success
cegex_status nfa_concat(
    nfa* ctx,
    nfa_fragment a,
    nfa_fragment b,
    nfa_fragment* out)
{
    assert(ctx != NULL);
    cegex_status status = CEGEX_OK;

    CEGEX_TRY(nfa_create_transition(ctx, a.final, b.initial, NFA_TRANSITION_EPSILON, NULL));
    if(out != NULL)
        *out = NFA_FRAGMENT(a.initial, b.final);

    return status;
fail:
    nfa_free(ctx);
    return status;
}

/// @brief Builds a new fragment, where `in` can be either absent or repeated (Kleene star)
/// @param ctx A initialized reference to a NFA
/// @param in The input fragment to be wrapped in the Kleene star expression
/// @param out The ouput fragment
/// @return An status code: `CEGEX_OK` on success
cegex_status nfa_star(
    nfa* ctx,
    nfa_fragment in,
    nfa_fragment* out)
{
    assert(ctx != NULL);
    cegex_status status = CEGEX_OK;

    nfa_state_id q0, qF;
    CEGEX_TRY(nfa_create_state(ctx, NFA_STATE_INITIAL, &q0));
    CEGEX_TRY(nfa_create_state(ctx, NFA_STATE_FINAL, &qF));

    CEGEX_TRY(nfa_create_transition(ctx, q0, in.initial, NFA_TRANSITION_EPSILON, NULL));
    CEGEX_TRY(nfa_create_transition(ctx, q0, qF, NFA_TRANSITION_EPSILON, NULL));

    CEGEX_TRY(nfa_create_transition(ctx, in.final, qF, NFA_TRANSITION_EPSILON, NULL));
    CEGEX_TRY(nfa_create_transition(ctx, in.final, in.initial, NFA_TRANSITION_EPSILON, NULL));

    if(out != NULL)
        *out = NFA_FRAGMENT(q0, qF);

    return status;
fail:
    nfa_free(ctx);
    return status;
}

bool nfa_state_id_eq(nfa_state_id* a,
                     nfa_state_id* b)
{
    return *a == *b;
}

void nfa_digraph_str(nfa* ctx,
                     nfa_fragment fragment,
                     char* buffer,
                     size_t n)
{
    assert(ctx != NULL);
    if(ctx->states.length == 0 || n == 0) {
        return;
    }

    nfa_state_id_array visited;
    nfa_state_id_array stack;
    ARRAY_INIT(&stack);
    ARRAY_INIT(&visited);

    size_t written = 0;
    written += (size_t) snprintf(buffer + written, n, "digraph G {\n\trankdir=\"LR\";\n\tnode[shape=circle];\n\n");
    if(written >= n)
        goto finally;

    char label[((NFA_TRANSITION_SET_MAX_SYMBOLS * 2) + 2) + 1];
    ARRAY_PUSH(&stack, fragment.initial);
    do {
        nfa_state_id top = 0;
        ARRAY_POP(&stack, &top);

        nfa_state* state = NULL;
        ARRAY_NTH_REF(&ctx->states, top, &state);

        if(state->is_final && state->id == fragment.final) {
            continue;
        }

        for(size_t i = 0; i < state->num_transitions; i++) {
            nfa_transition* transition = &state->transitions[i];
            if(transition->condition.type == NFA_TRANSITION_COND_EPSILON) {
                written += (size_t) snprintf(buffer + written, n, "\tq%zu -> q%zu [label=\"&#949;\"];\n", transition->from, transition->to);
                if(written >= n)
                    goto finally;
            } else {
                size_t label_length = 0;

                label[label_length++] = '{';
                for(size_t j = 0; j < transition->condition.set.length; j++) {
                    char symbol = transition->condition.set.elements[j];
                    label[label_length++] = symbol;

                    if(j + 1 != transition->condition.set.length)
                        label[label_length++] = ',';
                }
                label[label_length++] = '}';
                label[label_length] = '\0';

                written += (size_t) snprintf(buffer + written, n, "\tq%zu -> q%zu [label=\"%s\"];\n", transition->from, transition->to, label);
                if(written >= n)
                    goto finally;
            }

            if(!ARRAY_CONTAINS(&visited, transition->to, (comparator) nfa_state_id_eq)) {
                ARRAY_PUSH(&stack, transition->to);
                ARRAY_PUSH(&visited, transition->to);
            }
        }
    } while(!ARRAY_EMPTY(&stack));

    written += (size_t) snprintf(buffer + written, n,
                                 "\n"
                                 "\tstart -> q%zu;\n"
                                 "\tstart[shape=none, label=\"\"];\n"
                                 "\tq%zu[shape=doublecircle];\n"
                                 "}\n",
                                 fragment.initial, fragment.final);
finally:
    if(written >= n)
        written = n - 1;

    buffer[written] = '\0';

    ARRAY_FREE(&visited);
    ARRAY_FREE(&stack);
}

typedef struct nfa_match_entry
{
    size_t subject_index;
    nfa_state_id state_id;
} nfa_match_entry;

typedef ARRAY(nfa_match_entry) match_entry_array;

bool nfa_match_entry_eq(nfa_match_entry* a,
                        nfa_match_entry* b)
{
    return a->state_id == b->state_id && a->subject_index == b->subject_index;
}

bool single_byte_eq(byte* a,
                    byte* b)
{
    return *a == *b;
}

bool nfa_matches(const nfa* const ctx,
                 nfa_fragment fragment,
                 const char* subject,
                 size_t n)
{
    assert(ctx != NULL);
    assert(subject != NULL);
    bool matched = false;

    match_entry_array stack;
    match_entry_array visited;
    ARRAY_INIT(&stack);
    ARRAY_INIT(&visited);

    nfa_state* initial;
    ARRAY_NTH_REF(&ctx->states, fragment.initial, &initial);
    if(initial == NULL)
        goto end;

    ARRAY_PUSH(&stack, ((nfa_match_entry){
                           .state_id = initial->id,
                           .subject_index = 0 }));

    do {
        nfa_match_entry entry;
        ARRAY_POP(&stack, &entry);
        if(ARRAY_CONTAINS(&visited, entry, (comparator) nfa_match_entry_eq))
            continue;
        else
            ARRAY_PUSH(&visited, entry);

        nfa_state* state;
        ARRAY_NTH_REF(&ctx->states, entry.state_id, &state);

        if((state->is_final && state->id == fragment.final) && entry.subject_index == n) {
            matched = true;
            break;
        }

        for(size_t i = 0; i < state->num_transitions; i++) {
            nfa_transition* transition = &state->transitions[i];

            if(NFA_TRANSITION_COND_EPSILON == transition->condition.type) {
                ARRAY_PUSH(&stack, ((nfa_match_entry){
                                       .state_id = transition->to,
                                       .subject_index = entry.subject_index }));
            }

            if(NFA_TRANSITION_COND_SET == transition->condition.type) {
                if(entry.subject_index >= n)
                    continue;

                byte_set set = transition->condition.set;
                byte current = subject[entry.subject_index];
                if(ARRAY_CONTAINS(&set, current, (comparator) single_byte_eq)) {
                    ARRAY_PUSH(&stack, ((nfa_match_entry){
                                           .state_id = transition->to,
                                           .subject_index = entry.subject_index + 1 }));
                }
            }
        }
    } while(!ARRAY_EMPTY(&stack));

end:
    ARRAY_FREE(&stack);
    ARRAY_FREE(&visited);
    return matched;
}

typedef struct nfa_test_case
{
    const char* subject;
    size_t length;
    bool expected;
} nfa_test_case;

static const nfa_test_case test_cases[] = {
    { "c", 1, true },
    { "ac", 2, true },
    { "aac", 3, true },
    { "aaac", 4, true },
    { "abdc", 4, true },
    { "abec", 4, true },
    { "abdabdc", 7, true },
    { "abdabec", 7, true },
    { "aabdc", 5, true },
    { "abdaac", 6, true },
    { "aabdaac", 7, true },
    { "abdaabec", 8, true },
    { "aabdabdaac", 10, true },

    { "", 0, false },
    { "a", 1, false },
    { "ab", 2, false },
    { "abd", 3, false },
    { "abc", 3, false },
    { "abfc", 4, false },
    { "abcc", 4, false },
    { "abac", 4, false },
    { "bac", 3, false },
    { "dac", 3, false },
    { "acb", 3, false },
    { "acc", 3, false },
    { "aca", 3, false },
    { "abcd", 4, false },
    { "abdcx", 5, false },
    { "cc", 2, false },
    { "cac", 3, false },
    { "abecabdc", 8, false },
};

int main(void)
{
    cegex_status status = CEGEX_OK;
    nfa_fragment abde, de, a, a2, b, c, ab, unioned, repeated, root;

    nfa ctx;
    nfa_init(&ctx);

    CEGEX_TRY(NFA_CREATE_SYMBOLS(&ctx, ((byte[]){ 'd', 'e' }), &de));
    CEGEX_TRY(NFA_CREATE_SYMBOL(&ctx, 'a', &a));
    CEGEX_TRY(NFA_CREATE_SYMBOL(&ctx, 'a', &a2));
    CEGEX_TRY(NFA_CREATE_SYMBOL(&ctx, 'b', &b));
    CEGEX_TRY(NFA_CREATE_SYMBOL(&ctx, 'c', &c));

    CEGEX_TRY(nfa_concat(&ctx, a, b, &ab));          // ab
    CEGEX_TRY(nfa_concat(&ctx, ab, de, &abde));      // ab[de]
    CEGEX_TRY(nfa_union(&ctx, abde, a2, &unioned));  // ab[de]|a
    CEGEX_TRY(nfa_star(&ctx, unioned, &repeated));   // (ab[de]|a)*
    CEGEX_TRY(nfa_concat(&ctx, repeated, c, &root)); // (ab[de]|a)*c

    for(size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); ++i) {
        const nfa_test_case* test = &test_cases[i];

        bool actual = nfa_matches(
            &ctx,
            root,
            test->subject,
            test->length);

        printf("\"%s\" matches /%s/ : %s (expected: %s)\n", test->subject, "(ab[de]|a)*c", actual ? "true" : "false", test->expected ? "true" : "false");
    }

    char buffer[1024];
    nfa_digraph_str(&ctx, root, buffer, 1024);
    nfa_free(&ctx);

    // printf("%s", buffer);
    return 0;

fail:
    fprintf(stderr, "Error occured");
    nfa_free(&ctx);
    return status;
}

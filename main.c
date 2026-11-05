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

#define NFA_TRANSITION_EPSILON         ((nfa_transition_cond){ .type = NFA_TRANSITION_COND_EPSILON })
#define NFA_TRANSITION_SYMBOL(s)       ((nfa_transition_cond){ .type = NFA_TRANSITION_COND_SYMBOL, .as.symbol = s })

#define NFA_FRAGMENT(i, f)             ((nfa_fragment){ .initial = i, .final = f })

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

typedef size_t nfa_state_id;
typedef struct nfa_transition_set
{
    size_t num_symbols;
    char symbols[NFA_TRANSITION_SET_MAX_SYMBOLS];
} nfa_transition_set;

typedef struct nfa_transition_cond
{
    enum
    {
        NFA_TRANSITION_COND_EPSILON,
        NFA_TRANSITION_COND_SYMBOL,
        NFA_TRANSITION_COND_SET
    } type;

    union
    {
        char symbol;
        nfa_transition_set set;
    } as;
} nfa_transition_cond;

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

typedef struct nfa_builder
{
    struct
    {
        size_t length;
        size_t capacity;
        nfa_state* elements;
    } states;
} nfa_builder;

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

/// @brief Initializes the builder
/// @param builder A valid reference to a `nfa_builder` to initialize
/// @return `CEGEX_ERR_MEMORY` when allocation of internal array failed
cegex_status nfa_builder_init(nfa_builder* builder)
{
    assert(builder != NULL);

    ARRAY_INIT(&builder->states);
    if(builder->states.elements == NULL)
        return CEGEX_ERR_MEMORY;

    return CEGEX_OK;
}

void nfa_builder_free(nfa_builder* builder)
{
    assert(builder != NULL);

    if(builder->states.elements != NULL) {
        ARRAY_FREE(&builder->states);
    }
}

/// @brief Creates a transition from one state to another state using the provided transition condition `cond`
/// @param builder A initialized reference to a builder
/// @param from The source state id of the transition
/// @param to The destination state id of the transition
/// @param cond The transition condition, used to either allow or disallow the transition from the source state to the destination state
/// @return `CEGEX_ERR_INVALID_ARGUMENT` if the indicies are invalid or there are no transitions available in the `from` state
cegex_status nfa_builder_transition(
    nfa_builder* builder,
    nfa_state_id from,
    nfa_state_id to,
    nfa_transition_cond cond)
{
    assert(builder != NULL);

    // Are the state indices valid?
    if(ARRAY_EMPTY(&builder->states) || (builder->states.length <= from || builder->states.length <= to))
        return CEGEX_ERR_INVALID_ARGUMENT;

    nfa_state* from_state = NULL;
    ARRAY_NTH_REF(&builder->states, from, &from_state);

    // Are there still transitions available in the `from` state?
    if(from_state->num_transitions >= NFA_STATE_MAX_TRANSITIONS)
        return CEGEX_ERR_INVALID_ARGUMENT;

    from_state->transitions[from_state->num_transitions++] = (nfa_transition){
        .to = to,
        .from = from,
        .condition = cond
    };

    return CEGEX_OK;
}

/// @brief Pushes a new state to the `builder` internal array
/// @param builder A initialized reference to a builder
/// @param state The state to add
/// @param out The id of the newly added state
/// @return `CEGEX_ERR_LIMIT` when the state array is full
cegex_status nfa_builder_state(
    nfa_builder* builder,
    nfa_state state,
    nfa_state_id* out)
{
    assert(builder != NULL);

    if(builder->states.length >= NFA_MAX_STATES)
        return CEGEX_ERR_LIMIT;

    state.id = builder->states.length;
    ARRAY_PUSH(&builder->states, state);

    if(out != NULL)
        *out = state.id;

    return CEGEX_OK;
}

/// @brief Builds two states with an epsilon transition
/// @param builder A initialized reference to a builder
/// @param out The output fragment
/// @return An status code: `CEGEX_OK` on success
cegex_status nfa_builder_empty(nfa_builder* builder,
                               nfa_fragment* out)
{
    assert(builder != NULL);
    cegex_status status = CEGEX_OK;

    nfa_state_id q0, qF;
    CEGEX_TRY(nfa_builder_state(builder, NFA_STATE_INITIAL, &q0));
    CEGEX_TRY(nfa_builder_state(builder, NFA_STATE_FINAL, &qF));

    CEGEX_TRY(nfa_builder_transition(builder, q0, qF, NFA_TRANSITION_EPSILON));

    if(out != NULL)
        *out = NFA_FRAGMENT(q0, qF);

    return status;

fail:
    nfa_builder_free(builder);
    return status;
}

/// @brief Builds two states with one symbol transition
/// @param builder A initialized reference to a builder
/// @param symbol The symbol, used in the transition condition
/// @param out The output fragment
/// @return An status code: `CEGEX_OK` on success
cegex_status nfa_builder_symbol(
    nfa_builder* builder,
    char symbol,
    nfa_fragment* out)
{
    assert(builder != NULL);
    cegex_status status = CEGEX_OK;

    nfa_state_id q0, qF;
    CEGEX_TRY(nfa_builder_state(builder, NFA_STATE_INITIAL, &q0));
    CEGEX_TRY(nfa_builder_state(builder, NFA_STATE_FINAL, &qF));

    CEGEX_TRY(nfa_builder_transition(builder, q0, qF, NFA_TRANSITION_SYMBOL(symbol)));

    if(out != NULL)
        *out = NFA_FRAGMENT(q0, qF);

    return status;
fail:
    nfa_builder_free(builder);
    return status;
}

/// @brief Build the union of `a` and `b` (`a|b`)
/// @param builder A initialized reference to a builder
/// @param a The left side of the union expression
/// @param b The right side of the union expression
/// @param out The output fragment, where the initial state connects to both `a` and `b`, which in turn connect to a new shared final state
/// @return An status code: `CEGEX_OK` on success
cegex_status nfa_builder_union(
    nfa_builder* builder,
    nfa_fragment a,
    nfa_fragment b,
    nfa_fragment* out)
{
    assert(builder != NULL);
    cegex_status status = CEGEX_OK;

    nfa_state_id q0, qF;
    CEGEX_TRY(nfa_builder_state(builder, NFA_STATE_INITIAL, &q0));
    CEGEX_TRY(nfa_builder_state(builder, NFA_STATE_FINAL, &qF));

    CEGEX_TRY(nfa_builder_transition(builder, q0, a.initial, NFA_TRANSITION_EPSILON));
    CEGEX_TRY(nfa_builder_transition(builder, q0, b.initial, NFA_TRANSITION_EPSILON));

    CEGEX_TRY(nfa_builder_transition(builder, a.final, qF, NFA_TRANSITION_EPSILON));
    CEGEX_TRY(nfa_builder_transition(builder, b.final, qF, NFA_TRANSITION_EPSILON));

    if(out != NULL)
        *out = NFA_FRAGMENT(q0, qF);

    return status;
fail:
    nfa_builder_free(builder);
    return status;
}

/// @brief Build the concatination expression of `a` and `b`
/// @param builder A initialized reference to a builder
/// @param a The left side of the concatination
/// @param b The right side of the concatination
/// @param out The output fragment, initial state of `a` is connected to the initial state of `b`
/// @return An status code: `CEGEX_OK` on success
cegex_status nfa_builder_concat(
    nfa_builder* builder,
    nfa_fragment a,
    nfa_fragment b,
    nfa_fragment* out)
{
    assert(builder != NULL);
    cegex_status status = CEGEX_OK;

    CEGEX_TRY(nfa_builder_transition(builder, a.final, b.initial, NFA_TRANSITION_EPSILON));
    if(out != NULL)
        *out = NFA_FRAGMENT(a.initial, b.final);

    return status;
fail:
    nfa_builder_free(builder);
    return status;
}

/// @brief Builds a new fragment, where `in` can be either absent or repeated (Kleene star)
/// @param builder A initialized reference to a builder
/// @param in The input fragment to be wrapped in the Kleene star expression
/// @param out The ouput fragment
/// @return An status code: `CEGEX_OK` on success
cegex_status nfa_builder_star(
    nfa_builder* builder,
    nfa_fragment in,
    nfa_fragment* out)
{
    assert(builder != NULL);
    cegex_status status = CEGEX_OK;

    nfa_state_id q0, qF;
    CEGEX_TRY(nfa_builder_state(builder, NFA_STATE_INITIAL, &q0));
    CEGEX_TRY(nfa_builder_state(builder, NFA_STATE_FINAL, &qF));

    CEGEX_TRY(nfa_builder_transition(builder, q0, in.initial, NFA_TRANSITION_EPSILON));
    CEGEX_TRY(nfa_builder_transition(builder, q0, qF, NFA_TRANSITION_EPSILON));

    CEGEX_TRY(nfa_builder_transition(builder, in.final, qF, NFA_TRANSITION_EPSILON));
    CEGEX_TRY(nfa_builder_transition(builder, in.final, in.initial, NFA_TRANSITION_EPSILON));

    if(out != NULL)
        *out = NFA_FRAGMENT(q0, qF);

    return status;
fail:
    nfa_builder_free(builder);
    return status;
}

typedef struct nfa_state_id_array
{
    size_t capacity;
    size_t length;

    nfa_state_id* elements;
} nfa_state_id_array;

bool nfa_id_array_contains(nfa_state_id_array* array,
                           nfa_state_id id)
{
    for(size_t i = 0; i < array->length; i++) {
        nfa_state_id right = 0;
        ARRAY_NTH(array, i, &right);

        if(id == right)
            return true;
    }

    return false;
}

void nfa_digraph_str(nfa_builder* builder,
                     nfa_fragment fragment,
                     char* buffer,
                     size_t n)
{
    assert(builder != NULL);
    if(builder->states.length == 0 || n == 0) {
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

    ARRAY_PUSH(&stack, fragment.initial);
    do {
        nfa_state_id top = 0;
        ARRAY_POP(&stack, &top);

        nfa_state* state = NULL;
        ARRAY_NTH_REF(&builder->states, top, &state);

        if(state->is_final && state->id == fragment.final) {
            continue;
        }

        for(size_t i = 0; i < state->num_transitions; i++) {
            nfa_transition* transition = &state->transitions[i];
            if(transition->condition.type == NFA_TRANSITION_COND_EPSILON) {
                written += (size_t) snprintf(buffer + written, n, "\tq%zu -> q%zu [label=\"&#949;\"];\n", transition->from, transition->to);
                if(written >= n)
                    goto finally;
            } else if(transition->condition.type == NFA_TRANSITION_COND_SYMBOL) {
                written += (size_t) snprintf(buffer + written, n, "\tq%zu -> q%zu [label=\"%c\"];\n", transition->from, transition->to, transition->condition.as.symbol);
                if(written >= n)
                    goto finally;
            }

            if(!nfa_id_array_contains(&visited, transition->to)) {
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

int main(void)
{
    cegex_status status = CEGEX_OK;
    nfa_fragment a, b, c, ab, unioned, repeated, root;

    nfa_builder builder;
    nfa_builder_init(&builder);

    CEGEX_TRY(nfa_builder_symbol(&builder, 'a', &a));
    CEGEX_TRY(nfa_builder_symbol(&builder, 'b', &b));
    CEGEX_TRY(nfa_builder_concat(&builder, a, b, &ab));
    CEGEX_TRY(nfa_builder_symbol(&builder, 'a', &a));
    CEGEX_TRY(nfa_builder_union(&builder, ab, a, &unioned));
    CEGEX_TRY(nfa_builder_star(&builder, unioned, &repeated));
    CEGEX_TRY(nfa_builder_symbol(&builder, 'c', &c));
    CEGEX_TRY(nfa_builder_concat(&builder, repeated, c, &root));

    char buffer[1024];
    nfa_digraph_str(&builder, root, buffer, 1024);
    nfa_builder_free(&builder);

    printf("%s", buffer);
    return 0;

fail:
    fprintf(stderr, "Error occured");
    nfa_builder_free(&builder);
    return 1;
}

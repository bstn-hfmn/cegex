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
/// @param nfa A reference to the NFA containing the states
/// @param from The source state index of the transition
/// @param to The destination state index of the transition
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

/// @brief Appends a new state to the finite set of states (Q) of the NFA
/// @param nfa A reference to the NFA the state should be added to
/// @param state A copy of the state to be added
/// @param out Output-parameter for the id of the newly added state (nullable)
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

/// @brief Creates two states with an epsilon transition
/// @param out Output-parameter, where to store the created NFA
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

/// @brief Creates two states with an symbol transition condition
/// @param symbol The symbol required for the transition
/// @return The resulting symbol NFA
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

/*
/// @brief Finds the initial and final states in a range of states in the NFA
/// @param nfa The NFA to search in
/// @param initial Output parameter for the initial state (nullable)
/// @param final Output parameter for the final state (nullable)
/// @param offset The offset in the state array to start searching from
/// @param n The number of states to search through
cegex_status nfa_boundary_states(nfa* nfa,
                                 nfa_state** initial,
                                 nfa_state** final,
                                 size_t offset,
                                 size_t n)
{
    assert(nfa != NULL);
    if(nfa->num_states < offset + n)
        return CEGEX_ERR_LIMIT;

    for(size_t i = 0; i < n; i++) {
        nfa_state* state = &nfa->states[offset + i];
        if(state->is_initial && initial != NULL) {
            *initial = state;
            if(final == NULL)
                break;
        } else if(state->is_final && final != NULL) {
            *final = state;
            if(initial == NULL)
                break;
        }
    }

    return CEGEX_OK;
}

typedef struct nfa_state_copy_entry
{
    nfa_state_id src_id;
    nfa_state_id dst_id;
} nfa_state_copy_entry;


/// @brief Copies states from `src`, adds them to `dst` and patches the transitions
/// @param dst The destination NFA
/// @param src The source NFA
/// @param offset The offset in the source array
/// @param n The amount of states to copy from `src` starting from `offset`
/// @return The amount of copied states
size_t nfa_copy_states(
    nfa* dst,
    nfa* src,
    size_t offset,
    size_t n)
{
    assert(dst != NULL && src != NULL);
    nfa_state_copy_entry entries[NFA_MAX_STATES];

    size_t copied = 0;
    for(size_t i = 0; i < n; i++) {
        // Add all required states in bulk, building copy entries to patch transitions
        size_t index = offset + i;
        if(index >= src->num_states) {
            break;
        }

        nfa_state* src_state = &src->states[index];
        entries[i].src_id = src_state->id;

        if(CEGEX_OK != nfa_add_state(dst, *src_state, &entries[i].dst_id)) {
            goto patch;
        }

        ++copied;
    }

patch:
    for(size_t i = 0; i < copied; i++) {
        // Patch the transitions using the built copy entries
        nfa_state* dst_state = &dst->states[(dst->num_states - copied) + i];
        if(dst_state->num_transitions <= 0) {
            continue;
        }

        for(size_t j = 0; j < dst_state->num_transitions; j++) {
            // For each of the transition in the unpatched state
            nfa_transition* transition = &dst_state->transitions[j];
            nfa_state_id from = dst_state->id;
            nfa_state_id to = transition->to;
            for(size_t k = 0; k < copied; k++) {
                // Find the original entry and return the mapped identifier
                if(entries[k].src_id == transition->to) {
                    to = entries[k].dst_id;
                    break;
                }
            }

            // Patch the transition anchors
            transition->from = from;
            transition->to = to;
        }
    }

    return copied;
}
*/

/// @brief Creates a new NFA that is the union expression of a and b `a|b`
/// @param a Left side of the union expression
/// @param b Right side of the union expression
/// @return A new NFA, where the initial state (q0) has two epsilon transitions to `a` and `b` and the previous final states of `a` and `b` point to a new shared final state (qF)
cegex_status nfa_builder_union(
    nfa_builder* builder,
    nfa_fragment a,
    nfa_fragment b,
    nfa_fragment* out)
{
    assert(builder != NULL);

    cegex_status status = CEGEX_OK;

    /*    nfa_state_id q0, qF;
        CEGEX_TRY(nfa_add_state(&nfa, NFA_STATE_INITIAL, &q0));

        size_t offset = 1;
        size_t copied = nfa_copy_states(&nfa, a, 0, a->num_states);

        nfa_state *qa0 = NULL, *qaF = NULL;
        CEGEX_TRY(nfa_boundary_states(&nfa, &qa0, &qaF, offset, copied));
        qa0->is_initial = false;
        qaF->is_final = false;

        offset += copied;
        copied = nfa_copy_states(&nfa, b, 0, b->num_states);

        nfa_state *qb0 = NULL, *qbF = NULL;
        CEGEX_TRY(nfa_boundary_states(&nfa, &qb0, &qbF, offset, copied));
        qb0->is_initial = false;
        qbF->is_final = false;

        CEGEX_TRY(nfa_create_transition(&nfa, q0, qa0->id, NFA_TRANSITION_EPSILON));
        CEGEX_TRY(nfa_create_transition(&nfa, q0, qb0->id, NFA_TRANSITION_EPSILON));
    */

    nfa_state_id q0, qF;
    CEGEX_TRY(nfa_builder_state(builder, NFA_STATE_FINAL, &q0));
    CEGEX_TRY(nfa_builder_state(builder, NFA_STATE_INITIAL, &qF));

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

/// @brief The concatination expression of `a` and `b`
/// @param a Left side of the expression
/// @param b Right side of the expression
/// @return A new NFA, where `a` and `b` are stichted together in series, making the final state of `a` connect to the initial state of `b` and the final state of `b` becoming the final state of the new NFA
cegex_status nfa_builder_concat(
    nfa_builder* builder,
    nfa_fragment a,
    nfa_fragment b,
    nfa_fragment* out)
{
    assert(builder != NULL);
    cegex_status status = CEGEX_OK;
    /*
        size_t offset = 0;
        size_t copied = nfa_copy_states(&nfa, a, 0, a->num_states);

        nfa_state* qaF = NULL;
        CEGEX_TRY(nfa_boundary_states(&nfa, NULL, &qaF, offset, copied));
        qaF->is_final = false;

        offset += copied;
        copied = nfa_copy_states(&nfa, b, 0, b->num_states);

        nfa_state* qb0 = NULL;
        CEGEX_TRY(nfa_boundary_states(&nfa, &qb0, NULL, offset, copied));
        qb0->is_initial = false;
    */

    CEGEX_TRY(nfa_builder_transition(builder, a.final, b.initial, NFA_TRANSITION_EPSILON));
    if(out != NULL)
        *out = NFA_FRAGMENT(a.initial, b.final);

    return status;
fail:
    nfa_builder_free(builder);
    return status;
}

/// @brief Creates a new NFA where the input NFA can be either absent or repeated (Kleene star)
/// @param in The input NFA to be wrapped in the Kleene star expression
/// @return A new NFA, where `in` sits between a new initial and final state. The new initial state is directly connected to the new final state using an espilon transition. The final state of `in` is connected to the initial state of `in`, allowing for repetition of the inner expression
cegex_status nfa_builder_star(
    nfa_builder* builder,
    nfa_fragment in,
    nfa_fragment* out)
{
    assert(builder != NULL);

    cegex_status status = CEGEX_OK;

    /*    nfa_state_id q0, qF;
        CEGEX_TRY(nfa_add_state(&nfa, NFA_STATE_INITIAL, &q0));

        size_t offset = 1;
        size_t copied = nfa_copy_states(&nfa, in, 0, in->num_states);

        nfa_state *qin0 = NULL, *qinF = NULL;
        CEGEX_TRY(nfa_boundary_states(&nfa, &qin0, &qinF, offset, copied));
        qin0->is_initial = false;
        qinF->is_final = false;*/

    nfa_state_id q0, qF;
    CEGEX_TRY(nfa_builder_state(builder, NFA_STATE_FINAL, &q0));
    CEGEX_TRY(nfa_builder_state(builder, NFA_STATE_INITIAL, &qF));

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
                     nfa_fragment fragment)
{
    assert(builder != NULL);
    if(builder->states.length == 0) {
        printf("empty");
        return;
    }

    nfa_state_id_array visited;
    nfa_state_id_array stack;
    ARRAY_INIT(&stack);
    ARRAY_INIT(&visited);

    printf("digraph G {\n\trankdir=\"LR\";\n\tnode[shape=circle];\n\n");
    ARRAY_PUSH(&stack, fragment.initial);

    do {
        nfa_state_id top = 0;
        ARRAY_POP(&stack, &top);

        nfa_state* state = NULL;
        ARRAY_NTH_REF(&builder->states, top, &state);

        for(size_t i = 0; i < state->num_transitions; i++) {
            nfa_transition* transition = &state->transitions[i];
            if(transition->condition.type == NFA_TRANSITION_COND_EPSILON) {
                printf("\tq%zu -> q%zu [label=\"&#949;\"];\n", transition->from, transition->to);
            } else if(transition->condition.type == NFA_TRANSITION_COND_SYMBOL) {
                printf("\tq%zu -> q%zu [label=\"%c\"];\n", transition->from, transition->to, transition->condition.as.symbol);
            }

            if(!nfa_id_array_contains(&visited, transition->to)) {
                ARRAY_PUSH(&stack, transition->to);
                ARRAY_PUSH(&visited, transition->to);
            }
        }
    } while(!ARRAY_EMPTY(&stack));

    printf("\n");
    printf("\tstart -> q%zu;\n", fragment.initial);

    printf("\tstart[shape=none, label=\"\"];\n");
    printf("\tq%zu[shape=doublecircle];\n", fragment.final);

    printf("}\n");

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

    nfa_digraph_str(&builder, root);
    nfa_builder_free(&builder);
    return 0;

fail:
    fprintf(stderr, "Error occured");
    nfa_builder_free(&builder);
    return 1;
}

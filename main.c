#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define NFA_MAX_STATES                 256
#define NFA_STATE_MAX_TRANSITIONS      2
#define NFA_TRANSITION_SET_MAX_SYMBOLS 64

#define NFA_TRANSITION_EPSILON         ((nfa_transition_cond){ .type = NFA_TRANSITION_COND_EPSILON })
#define NFA_TRANSITION_SYMBOL(s)       ((nfa_transition_cond){ .type = NFA_TRANSITION_COND_SYMBOL, .as.symbol = s })

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

typedef struct nfa
{
    size_t num_states;
    nfa_state states[NFA_MAX_STATES];
} nfa;

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

/// @brief Creates a transition from one state to another state using the provided transition condition `cond`
/// @param nfa A reference to the NFA containing the states
/// @param from The source state index of the transition
/// @param to The destination state index of the transition
/// @param cond The transition condition, used to either allow or disallow the transition from the source state to the destination state
/// @return `true` if the transition was created, `false` when there was an error
bool nfa_create_transition(
    nfa* nfa,
    nfa_state_id from,
    nfa_state_id to,
    nfa_transition_cond cond)
{
    if(nfa == NULL)
        return false;

    // Are the state indices valid?
    if(nfa->num_states <= 0 || (nfa->num_states <= from || nfa->num_states <= to))
        return false;

    nfa_state* from_state = &nfa->states[from];

    // Are there still transitions available in the `from` state?
    if(from_state->num_transitions >= NFA_STATE_MAX_TRANSITIONS)
        return false;

    from_state->transitions[from_state->num_transitions++] = (nfa_transition){
        .to = to,
        .from = from,
        .condition = cond
    };

    return true;
}

/// @brief Appends a new state to the finite set of states (Q) of the NFA
/// @param nfa A reference to the NFA the state should be added to
/// @param state A copy of the state to be added
/// @param id Output-parameter for the id of the newly added state (nullable)
/// @return `NULL` when failed or a `nfa_state*` to the newly added state
nfa_state* nfa_add_state(
    nfa* nfa,
    nfa_state state,
    nfa_state_id* out_id)
{
    assert(nfa != NULL);
    if(nfa->num_states >= NFA_MAX_STATES) {
        return NULL;
    }

    size_t index = nfa->num_states;
    nfa->states[nfa->num_states++] = state;
    nfa->states[index].id = index;

    if(out_id != NULL)
        *out_id = nfa->states[index].id;

    return &nfa->states[index];
}

/// @brief Creates two states with an epsilon transition
/// @return The resulting empty NFA
nfa nfa_create_empty(void)
{
    nfa nfa = { 0 };

    nfa_state_id q0, qF;
    assert(nfa_add_state(&nfa, NFA_STATE_INITIAL, &q0) != NULL);
    assert(nfa_add_state(&nfa, NFA_STATE_FINAL, &qF) != NULL);

    nfa_create_transition(&nfa, q0, qF, NFA_TRANSITION_EPSILON);
    return nfa;
}

/// @brief Creates two states with an symbol transition condition
/// @param symbol The symbol required for the transition
/// @return The resulting symbol NFA
nfa nfa_create_symbol(
    char symbol)
{
    nfa nfa = { 0 };

    nfa_state_id q0, qF;
    assert(nfa_add_state(&nfa, NFA_STATE_INITIAL, &q0) != NULL);
    assert(nfa_add_state(&nfa, NFA_STATE_FINAL, &qF) != NULL);

    nfa_create_transition(&nfa, q0, qF, NFA_TRANSITION_SYMBOL(symbol));
    return nfa;
}

void nfa_boundary_states(nfa* nfa,
                         nfa_state** initial,
                         nfa_state** final,
                         size_t offset,
                         size_t n)
{
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

        // Added states will have their id's patched by `nfa_add_state`
        assert(nfa_add_state(dst, *src_state, &entries[i].dst_id) != NULL);
        ++copied;
    }

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

/// @brief Creates a new NFA that is the union expression of a and b `a|b`
/// @param a Left side of the union expression
/// @param b Right side of the union expression
/// @return A new NFA, where the initial state (q0) has two epsilon transitions to `a` and `b` and the previous final states of `a` and `b` point to a new shared final state (qF)
nfa nfa_create_union(
    nfa* a,
    nfa* b)
{
    nfa nfa = { 0 };

    nfa_state_id q0, qF;
    assert(nfa_add_state(&nfa, NFA_STATE_INITIAL, &q0) != NULL);

    size_t offset = 1;
    size_t copied = nfa_copy_states(&nfa, a, 0, a->num_states);

    nfa_state *qa0 = NULL, *qaF = NULL;
    nfa_boundary_states(&nfa, &qa0, &qaF, offset, copied);

    assert(qa0 != NULL && qaF != NULL);
    qa0->is_initial = false;
    qaF->is_final = false;

    offset += copied;
    copied = nfa_copy_states(&nfa, b, 0, b->num_states);

    nfa_state *qb0 = NULL, *qbF = NULL;
    nfa_boundary_states(&nfa, &qb0, &qbF, offset, copied);

    assert(qb0 != NULL && qbF != NULL);
    qb0->is_initial = false;
    qbF->is_final = false;

    nfa_create_transition(&nfa, q0, qa0->id, NFA_TRANSITION_EPSILON);
    nfa_create_transition(&nfa, q0, qb0->id, NFA_TRANSITION_EPSILON);

    nfa_add_state(&nfa, NFA_STATE_FINAL, &qF);
    nfa_create_transition(&nfa, qaF->id, qF, NFA_TRANSITION_EPSILON);
    nfa_create_transition(&nfa, qbF->id, qF, NFA_TRANSITION_EPSILON);

    return nfa;
}

/// @brief The concatination expression of `a` and `b`
/// @param a Left side of the expression
/// @param b Right side of the expression
/// @return A new NFA, where `a` and `b` are stichted together in series, making the final state of `a` connect to the initial state of `b` and the final state of `b` becoming the final state of the new NFA
nfa nfa_create_concat(
    nfa* a,
    nfa* b)
{
    nfa nfa = { 0 };

    size_t offset = 0;
    size_t copied = nfa_copy_states(&nfa, a, 0, a->num_states);

    nfa_state* qaF = NULL;
    nfa_boundary_states(&nfa, NULL, &qaF, offset, copied);

    assert(qaF != NULL);
    qaF->is_final = false;

    offset += copied;
    copied = nfa_copy_states(&nfa, b, 0, b->num_states);

    nfa_state* qb0 = NULL;
    nfa_boundary_states(&nfa, &qb0, NULL, offset, copied);

    assert(qb0 != NULL);
    qb0->is_initial = false;

    nfa_create_transition(&nfa, qaF->id, qb0->id, NFA_TRANSITION_EPSILON);
    return nfa;
}

/// @brief Creates a new NFA where the input NFA can be either absent or repeated (Kleene star)
/// @param in The input NFA to be wrapped in the Kleene star expression
/// @return A new NFA, where `in` sits between a new initial and final state. The new initial state is directly connected to the new final state using an espilon transition. The final state of `in` is connected to the initial state of `in`, allowing for repetition of the inner expression
nfa nfa_create_star(
    nfa* in)
{
    nfa nfa = { 0 };

    size_t q0, qF;
    assert(nfa_add_state(&nfa, NFA_STATE_INITIAL, &q0) != NULL);

    size_t offset = 1;
    size_t copied = nfa_copy_states(&nfa, in, 0, in->num_states);

    nfa_state *qin0 = NULL, *qinF = NULL;
    nfa_boundary_states(&nfa, &qin0, &qinF, offset, copied);

    assert(qin0 != NULL && qinF != NULL);
    qin0->is_initial = false;
    qinF->is_final = false;

    nfa_create_transition(&nfa, q0, qin0->id, NFA_TRANSITION_EPSILON);
    nfa_create_transition(&nfa, qinF->id, qin0->id, NFA_TRANSITION_EPSILON);
    assert(nfa_add_state(&nfa, NFA_STATE_FINAL, &qF) != NULL);

    nfa_create_transition(&nfa, qinF->id, qF, NFA_TRANSITION_EPSILON);
    nfa_create_transition(&nfa, q0, qF, NFA_TRANSITION_EPSILON);

    return nfa;
}

void nfa_digraph_str(nfa* nfa)
{
    assert(nfa != NULL);

    printf("digraph G {\n\trankdir=\"LR\";\n\tnode[shape=circle];\n\n");
    for(size_t i = 0; i < nfa->num_states; i++) {
        nfa_state* state = &nfa->states[i];
        if(state->num_transitions <= 0)
            continue;

        for(size_t j = 0; j < state->num_transitions; j++) {
            nfa_transition* transition = &state->transitions[j];
            if(transition->condition.type == NFA_TRANSITION_COND_EPSILON) {
                printf("\tq%zu -> q%zu [label=\"&#949;\"];\n", transition->from, transition->to);
            } else if(transition->condition.type == NFA_TRANSITION_COND_SYMBOL) {
                printf("\tq%zu -> q%zu [label=\"%c\"];\n", transition->from, transition->to, transition->condition.as.symbol);
            }
        }
    }

    printf("\n");
    if(nfa->num_states > 0) {
        for(size_t i = 0; i < nfa->num_states; i++) {
            if(nfa->states[i].is_initial) {
                printf("\tstart -> q%zu;\n", nfa->states[i].id);
                break;
            }
        }

        printf("\tstart[shape=none, label=\"\"];\n");
    }

    for(size_t i = 0; i < nfa->num_states; i++) {
        if(nfa->states[i].is_final) {
            printf("\tq%zu[shape=doublecircle];\n", nfa->states[i].id);
        }
    }

    printf("}\n");
}

int main(void)
{
    nfa a = nfa_create_symbol('a');
    nfa b = nfa_create_symbol('b');
    nfa c = nfa_create_symbol('c');

    nfa ab = nfa_create_concat(&a, &b);     // ab
    nfa aba = nfa_create_union(&ab, &a);    // (ab)|a
    nfa aas = nfa_create_star(&aba);        // ((ab)|a)*
    nfa aasc = nfa_create_concat(&aas, &c); // ((ab)|a)*c

    nfa_digraph_str(&aasc);

    return 0;
}

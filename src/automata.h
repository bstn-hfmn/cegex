#ifndef CEGEX_AUTOMATA_H
#define CEGEX_AUTOMATA_H

#include "common.h"
#include "util/collections.h"

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

typedef struct nfa_match_entry
{
    size_t subject_index;
    nfa_state_id state_id;
} nfa_match_entry;

typedef ARRAY(nfa_state) nfa_state_array;
typedef ARRAY(nfa_fragment) nfa_fragment_array;
typedef ARRAY(nfa_state_id) nfa_state_id_array;
typedef ARRAY(nfa_match_entry) match_entry_array;

typedef struct nfa
{
    nfa_state_array states;
} nfa;

/// @brief Checks if two `nfa_state_id` are equal (`a == b`)
/// @param a Left side of the expression
/// @param b Right side of the expression
/// @return If the two ids are equal
bool nfa_state_id_eq(nfa_state_id* a,
                     nfa_state_id* b);

/// @brief Checks if two `nfa_match_entry` structs are equal (`a == b`)
/// @param a Left side of the expression
/// @param b Right side of the expression
/// @return If the two entries are equal
bool nfa_match_entry_eq(nfa_match_entry* a,
                        nfa_match_entry* b);

/// @brief Checks if `a` is equal to `b` (`a == b`)
/// @param a Left side of the expression
/// @param b Right side of the expression
/// @return If the two bytes are equal
bool single_byte_eq(byte* a,
                    byte* b);

/// @brief Initializes the NFA
/// @param ctx A valid reference to a `nfa` to initialize
/// @return `CEGEX_ERR_MEMORY` when allocation of internal array failed
cegex_status nfa_init(nfa* ctx);

void nfa_free(nfa* ctx);

/// @brief Creates a transition from one state to another state using the provided transition condition `cond`
/// @param ctx Pointer to the NFA context containing the states
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
    nfa_transition** out);

/// @brief Pushes a new state to the `ctx` internal array of states
/// @param ctx Pointer to the NFA context containing the states
/// @param state The state to add
/// @param out The id of the newly added state
/// @return `CEGEX_ERR_LIMIT` when the state array is full
cegex_status nfa_create_state(
    nfa* ctx,
    nfa_state state,
    nfa_state_id* out);

/// @brief Builds two states with an epsilon transition
/// @param ctx Pointer to the NFA context containing the states
/// @param out The output fragment
/// @return An status code: `CEGEX_OK` on success
cegex_status nfa_create_empty(nfa* ctx,
                              nfa_fragment* out);

/// @brief Builds two states with a transition and a given set of bytes as the transition condition (expands the alphabet of the NFA)
/// @param ctx Pointer to the NFA context containing the states
/// @param bytes The set of bytes, used as transition condition
/// @param out The output fragment
/// @return An status code: `CEGEX_OK` on success
cegex_status nfa_create_symbol(
    nfa* ctx,
    byte* bytes,
    size_t n,
    nfa_fragment* out);

/// @brief Build the union of `a` and `b` (`a|b`)
/// @param ctx Pointer to the NFA context containing the states
/// @param a The left side of the union expression
/// @param b The right side of the union expression
/// @param out The output fragment, where the initial state connects to both `a` and `b`, which in turn connect to a new shared final state
/// @return An status code: `CEGEX_OK` on success
cegex_status nfa_union(
    nfa* ctx,
    nfa_fragment a,
    nfa_fragment b,
    nfa_fragment* out);

/// @brief Build the concatination expression of `a` and `b`
/// @param ctx Pointer to the NFA context containing the states
/// @param a The left side of the concatination
/// @param b The right side of the concatination
/// @param out The output fragment, initial state of `a` is connected to the initial state of `b`
/// @return An status code: `CEGEX_OK` on success
cegex_status nfa_concat(
    nfa* ctx,
    nfa_fragment a,
    nfa_fragment b,
    nfa_fragment* out);

/// @brief Builds a new fragment, where `in` can be either absent or repeated (Kleene star)
/// @param ctx Pointer to the NFA context containing the states
/// @param in The input fragment to be wrapped in the Kleene star expression
/// @param out The ouput fragment
/// @return An status code: `CEGEX_OK` on success
cegex_status nfa_star(
    nfa* ctx,
    nfa_fragment in,
    nfa_fragment* out);

/// @brief Checks whether the given input string is accepted by the NFA fragment
/// @param ctx Pointer to the NFA context containing the states
/// @param fragment The NFA fragment (sub-automaton) to evaluate against the input
/// @param subject Pointer to the input character buffer to match
/// @param n The length of the input string in bytes
/// @return `true` if the NFA fragment accepts the input string, `false` otherwise
bool nfa_matches(const nfa* const ctx,
                 nfa_fragment fragment,
                 const char* subject,
                 size_t n);

#endif
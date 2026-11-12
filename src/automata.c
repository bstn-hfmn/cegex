#include "automata.h"
#include <assert.h>

bool nfa_state_id_eq(nfa_state_id* a,
                     nfa_state_id* b)
{
    return *a == *b;
}

bool nfa_match_entry_eq(nfa_match_entry* a,
                        nfa_match_entry* b)
{
    return a->state_id == b->state_id && a->subject_index == b->subject_index;
}

bool single_byte_eq(cbyte* a,
                    cbyte* b)
{
    return *a == *b;
}

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

cegex_status nfa_create_symbol(
    nfa* ctx,
    cbyte* bytes,
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
                cbyte current = (cbyte) subject[entry.subject_index];
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
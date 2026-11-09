#include "debug.h"
#include <assert.h>
#include <stdio.h>

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
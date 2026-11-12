#ifndef CEGEX_UTIL_DEBUG
#define CEGEX_UTIL_DEBUG

#include "../automata.h"
#include "../tokenizer.h"

void cegex_nfa_digraph_str(nfa* ctx,
                           nfa_fragment fragment,
                           char* buffer,
                           size_t n);

const char* cegex_token_str(cegex_token_type type);

#endif
#ifndef CEGEX_UTIL_DEBUG
#define CEGEX_UTIL_DEBUG

#include "../automata.h"

void nfa_digraph_str(nfa* ctx,
                     nfa_fragment fragment,
                     char* buffer,
                     size_t n);

#endif
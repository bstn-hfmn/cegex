#ifndef CEGEX_UTIL_DEBUG
#define CEGEX_UTIL_DEBUG

#include "../automata.h"
#include "../parser.h"
#include "../token.h"

void cegex_parse_node_str(cegex_parse_node* node,
                          char* buffer,
                          size_t n);

void cegex_nfa_digraph_str(nfa* ctx,
                           nfa_fragment fragment,
                           char* buffer,
                           size_t n);

const char* cegex_parse_node_type_str(cegex_parse_node_type type);
const char* cegex_token_str(cegex_token_type type);

#endif
#ifndef CEGEX_TOKENIZER_H
#define CEGEX_TOKENIZER_H

#include "common.h"
#include "token.h"

typedef struct cegex_tokenized_expr
{
    cegex_uchar* source;
    cegex_token_array tokens;
} cegex_tokenized_expr;

typedef struct cegex_tokenizer
{
    size_t num_tokenized;
    cegex_tokenized_expr tokenized[CEGEX_EXPRESSION_CAPACITY];
} cegex_tokenizer;

cegex_status cegex_tokenizer_init(cegex_tokenizer* ctx);

/// @attention The resulting `out` array is only valid for as long as the tokenizer is alive (shared lifteime)
/// @brief Tokenizes the given string
/// @param ctx Pointer to the tokenizer context containing the token
/// @param source String to tokenize
/// @param n Length of the input string
/// @param out Output-parameter for the resulting array of tokens
/// @return `CEGEX_OK` on success
cegex_status cegex_tokenizer_exec(cegex_tokenizer* ctx,
                                  const char* source,
                                  size_t length,
                                  cegex_tokenized_expr** out);

void cegex_tokenizer_free(cegex_tokenizer* ctx);

#endif
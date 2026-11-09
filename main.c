#include <stdio.h>

#include "./src/automata.h"
#include "./src/util/debug.h"
#include <assert.h>

#define CEGEX_TOKENIZER_TOKEN_ARRAYS_CAPACITY 32

typedef enum cegex_token_type
{
    CEGEX_TOKEN_UNKNOWN = 0,
    CEGEX_TOKEN_CHARACTER,

    CEGEX_TOKEN_CLASS_ANY,
    CEGEX_TOKEN_CLASS_WORD,
    CEGEX_TOKEN_CLASS_NOT_WORD,
    CEGEX_TOKEN_CLASS_DIGIT,
    CEGEX_TOKEN_CLASS_NOT_DIGIT,
    CEGEX_TOKEN_CLASS_WHITESPACE,
    CEGEX_TOKEN_CLASS_NOT_WHITESPACE,

    CEGEX_TOKEN_ANCHOR_START,
    CEGEX_TOKEN_ANCHOR_END,
    CEGEX_TOKEN_ANCHOR_BOUNDARY,
    CEGEX_TOKEN_ANCHOR_NOT_BOUNDARY,

    CEGEX_TOKEN_ESCAPE_SEQUENCE,

    CEGEX_TOKEN_STAR,
    CEGEX_TOKEN_PLUS,
    CEGEX_TOKEN_QUESTION_MARK,
    CEGEX_TOKEN_COLON,
    CEGEX_TOKEN_HAT,
    CEGEX_TOKEN_ALTERNATE,
    CEGEX_TOKEN_PAREN_OPEN,
    CEGEX_TOKEN_PAREN_CLOSE,
    CEGEX_TOKEN_SQUARE_OPEN,
    CEGEX_TOKEN_SQUARE_CLOSE,
    CEGEX_TOKEN_CURLY_OPEN,
    CEGEX_TOKEN_CURLY_CLOSE,
    CEGEX_TOKEN_COMMA
} cegex_token_type;

typedef struct cegex_token
{
    size_t start;
    size_t length;
    cegex_token_type type;
} cegex_token;

typedef ARRAY(cegex_token) cegex_token_array;

typedef struct cegex_tokenizer
{
    char* source;
    size_t source_index;
    size_t source_length;

    size_t token_arrays_length;
    cegex_token_array token_arrays[CEGEX_TOKENIZER_TOKEN_ARRAYS_CAPACITY];
} cegex_tokenizer;

cegex_status cegex_tokenizer_init(cegex_tokenizer* ctx)
{
    assert(ctx != NULL);

    ctx->source = NULL;
    ctx->source_index = 0;
    ctx->source_length = 0;
    ctx->token_arrays_length = 0;

    return CEGEX_OK;
}

cegex_token_array* cegex_tokenizer_active_array(cegex_tokenizer* ctx)
{
    if(ctx->token_arrays_length == 0)
        return NULL;

    return &ctx->token_arrays[ctx->token_arrays_length - 1];
}

cegex_status cegex_tokenizer_exec(cegex_tokenizer* ctx,
                                  const char* source,
                                  size_t n,
                                  cegex_token_array* out)
{
    assert(ctx != NULL);
    if(ctx->token_arrays_length >= CEGEX_TOKENIZER_TOKEN_ARRAYS_CAPACITY) {
        return CEGEX_ERR_LIMIT;
    }

    ARRAY_INIT(&ctx->token_arrays[ctx->token_arrays_length++]);
    return CEGEX_OK;
}

void cegex_tokenizer_free(cegex_tokenizer* ctx)
{
    assert(ctx != NULL);

    for(size_t i = 0; i < ctx->token_arrays_length; i++) {
        ARRAY_FREE(&ctx->token_arrays[i]);
    }
}

int main(void)
{
    cegex_tokenizer ctx;
    cegex_tokenizer_init(&ctx);

    cegex_token_array tokens;
    cegex_tokenizer_exec(&ctx, "(ab[de]|a)*c", 13, &tokens);

    cegex_tokenizer_free(&ctx);
}

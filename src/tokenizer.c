#include "tokenizer.h"
#include <assert.h>
#include <ctype.h>

cegex_token_type cegex_token_type_from_char(char needle)
{
    switch(needle) {
    case '.':
        return CEGEX_TOKEN_CLASS_DOT;
    case '^':
        return CEGEX_TOKEN_ANCHOR_START;
    case '$':
        return CEGEX_TOKEN_ANCHOR_END;
    case '*':
        return CEGEX_TOKEN_STAR;
    case '+':
        return CEGEX_TOKEN_PLUS;
    case '?':
        return CEGEX_TOKEN_QUESTION_MARK;
    case ':':
        return CEGEX_TOKEN_COLON;
    case '|':
        return CEGEX_TOKEN_ALTERNATE;
    case '(':
        return CEGEX_TOKEN_PAREN_OPEN;
    case ')':
        return CEGEX_TOKEN_PAREN_CLOSE;
    case '[':
        return CEGEX_TOKEN_SQUARE_OPEN;
    case ']':
        return CEGEX_TOKEN_SQUARE_CLOSE;
    case '{':
        return CEGEX_TOKEN_CURLY_OPEN;
    case '}':
        return CEGEX_TOKEN_CURLY_CLOSE;
    case ',':
        return CEGEX_TOKEN_COMMA;
    case '-':
        return CEGEX_TOKEN_MINUS;
    default:
        return CEGEX_TOKEN_CHARACTER;
    }
}

cegex_status cegex_tokenizer_init(cegex_tokenizer* ctx)
{
    assert(ctx != NULL);
    ctx->token_arrays_length = 0;

    return CEGEX_OK;
}

static int cegex_isodigit(int c)
{
    return c >= '0' && c <= '7';
}

void cegex_tokenizer_consume_digits(cegex_tokenizer* ctx,
                                    int (*pred)(int),
                                    size_t len)
{
    char peeked;
    for(size_t i = 0; i < len; i++) {
        if((CEGEX_OK != ITERATOR_PEEK(&ctx->iterator, &peeked)) || !pred((unsigned char) peeked))
            break;

        ITERATOR_NEXT(&ctx->iterator);
    }
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

    cegex_status status = CEGEX_OK;
    cegex_token_array array = { 0 };

    CEGEX_TRY(ARRAY_INIT(&array));
    ITERATOR_INIT(&ctx->iterator, source, n);

    while(!ITERATOR_EMPTY(&ctx->iterator)) {
        char current;
        CEGEX_TRY(ITERATOR_CURRENT(&ctx->iterator, &current));

        cegex_token token = { 0 };
        token.start = ctx->iterator.index;

        if(current == '\\') {
            // Escape sequence or perl character class
            if(CEGEX_OK != ITERATOR_NEXT(&ctx->iterator)) {
                status = CEGEX_ERR_SYNTAX;
                goto fail;
            }

            CEGEX_TRY(ITERATOR_CURRENT(&ctx->iterator, &current));

            token.flags |= TOKEN_ESC;
            switch(current) {
                // Perl character classes
            case 'w':
                token.type = CEGEX_TOKEN_CLASS_WORD;
                token.flags |= TOKEN_PERL;
                break;
            case 'W':
                token.type = CEGEX_TOKEN_CLASS_NOT_WORD;
                token.flags |= TOKEN_PERL;
                break;
            case 'd':
                token.type = CEGEX_TOKEN_CLASS_DIGIT;
                token.flags |= TOKEN_PERL;
                break;
            case 'D':
                token.type = CEGEX_TOKEN_CLASS_NOT_DIGIT;
                token.flags |= TOKEN_PERL;
                break;
            case 's':
                token.type = CEGEX_TOKEN_CLASS_WHITESPACE;
                token.flags |= TOKEN_PERL;
                break;
            case 'S':
                token.type = CEGEX_TOKEN_CLASS_NOT_WHITESPACE;
                token.flags |= TOKEN_PERL;
                break;

                // Anchors
            case 'b':
                token.type = CEGEX_TOKEN_ANCHOR_BOUNDARY;
                break;
            case 'B':
                token.type = CEGEX_TOKEN_ANCHOR_NOT_BOUNDARY;
                break;

                // Supported escape sequences
            case 'a':
                token.type = CEGEX_TOKEN_ESCAPE_BELL;
                break;
            case 'f':
                token.type = CEGEX_TOKEN_ESCAPE_FORM_FEED;
                break;
            case 't':
                token.type = CEGEX_TOKEN_ESCAPE_HORIZONTAL_TAB;
                break;
            case 'n':
                token.type = CEGEX_TOKEN_ESCAPE_NEWLINE;
                break;
            case 'r':
                token.type = CEGEX_TOKEN_ESCAPE_CARRIAGE_RETURN;
                break;
            case 'v':
                token.type = CEGEX_TOKEN_ESCAPE_VERTICAL_TAB;
                break;

            default: {
                if(cegex_isodigit(current)) {
                    token.type = CEGEX_TOKEN_ESCAPE_OCTAL;
                    ctx->iterator.index -= 1;
                } else if(current == 'x') {
                    token.type = CEGEX_TOKEN_ESCAPE_HEX;
                } else {
                    status = CEGEX_ERR_SYNTAX;
                    goto fail;
                }
            } break;
            }
        } else {
            token.type = cegex_token_type_from_char(current);
            token.flags |= TOKEN_CHARACTER;

            if(CEGEX_TOKEN_CHARACTER != token.type) {
                token.flags |= TOKEN_SPECIAL;
            }

            if(isdigit(current)) {
                token.flags |= TOKEN_NUMBER;
            }

            if(isxdigit(current)) {
                token.flags |= TOKEN_HEX_DIGIT;
            }

            if(cegex_isodigit(current)) {
                token.flags |= TOKEN_OCT_DIGIT;
            }
        }

        ITERATOR_NEXT(&ctx->iterator);

        token.length = ctx->iterator.index - token.start;
        CEGEX_TRY(ARRAY_PUSH(&array, token));
    }

    ctx->token_arrays[ctx->token_arrays_length++] = array;

    if(out != NULL)
        *out = ctx->token_arrays[ctx->token_arrays_length - 1];

    return CEGEX_OK;

fail:
    ARRAY_FREE(&array);
    return status;
}

void cegex_tokenizer_free(cegex_tokenizer* ctx)
{
    assert(ctx != NULL);

    for(size_t i = 0; i < ctx->token_arrays_length; i++) {
        ARRAY_FREE(&ctx->token_arrays[i]);
    }
}
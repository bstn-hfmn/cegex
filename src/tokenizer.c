#include "tokenizer.h"
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

cegex_token_type cegex_token_type_from_char(char needle)
{
    switch(needle) {
    case '.':
        return CEGEX_TOKEN_CLASS_DOT;
    case '^':
        return CEGEX_TOKEN_HAT;
    case '$':
        return CEGEX_TOKEN_DOLLAR;
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

static int cegex_isodigit(int c)
{
    return c >= '0' && c <= '7';
}

cegex_status cegex_tokenizer_exec(cegex_tokenizer* ctx,
                                  const char* source,
                                  size_t length,
                                  cegex_tokenized_expr* out)
{
    assert(ctx != NULL);
    if(ctx->num_tokenized >= CEGEX_EXPRESSION_CAPACITY) {
        return CEGEX_ERR_LIMIT;
    }

    cegex_status status = CEGEX_OK;
    cegex_token_array array = { 0 };

    CEGEX_TRY(ARRAY_INIT(&array));

    uchar_iterator iterator;
    ITERATOR_INIT(&iterator, source, length);

    while(!ITERATOR_EMPTY(&iterator)) {
        char current;
        CEGEX_TRY(ITERATOR_CURRENT(&iterator, &current));

        cegex_token token = { 0 };
        token.start = iterator.index;

        if(current == '\\') {
            // Escape sequence or perl character class
            if(CEGEX_OK != ITERATOR_NEXT(&iterator)) {
                status = CEGEX_ERR_SYNTAX;
                goto fail;
            }

            CEGEX_TRY(ITERATOR_CURRENT(&iterator, &current));

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
                    iterator.index -= 1;
                } else if(current == 'x') {
                    token.type = CEGEX_TOKEN_ESCAPE_HEX;
                } else {
                    token.type = CEGEX_TOKEN_CHARACTER;
                }
            } break;
            }
        } else {
            token.type = cegex_token_type_from_char(current);
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

        ITERATOR_NEXT(&iterator);

        token.length = iterator.index - token.start;
        CEGEX_TRY(ARRAY_PUSH(&array, token));
    }

    ctx->tokenized[ctx->num_tokenized].source = malloc(sizeof(char) * length + 1);
    memcpy(ctx->tokenized[ctx->num_tokenized].source, source, length);
    ctx->tokenized[ctx->num_tokenized].source[length] = '\0';
    ctx->tokenized[ctx->num_tokenized].tokens = array;

    if(out != NULL) {
        *out = ctx->tokenized[ctx->num_tokenized];
    }

    ctx->num_tokenized += 1;
    return CEGEX_OK;

fail:
    ARRAY_FREE(&array);
    return status;
}

void cegex_tokenizer_free(cegex_tokenizer* ctx)
{
    assert(ctx != NULL);

    for(size_t i = 0; i < ctx->num_tokenized; i++) {
        ARRAY_FREE(&ctx->tokenized->tokens);
        free(ctx->tokenized->source);
    }
}
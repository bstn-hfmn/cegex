#include <stdio.h>

#include "./src/automata.h"
#include "./src/util/debug.h"
#include <assert.h>
#include <ctype.h>
#include <string.h>

#define CEGEX_TOKENIZER_TOKEN_ARRAYS_CAPACITY 32

typedef enum cegex_token_type
{
    CEGEX_TOKEN_UNKNOWN = 0,
    CEGEX_TOKEN_CHARACTER,
    CEGEX_TOKEN_CLASS_ANY,            // .
    CEGEX_TOKEN_CLASS_WORD,           // \w
    CEGEX_TOKEN_CLASS_NOT_WORD,       // \W
    CEGEX_TOKEN_CLASS_DIGIT,          // \d
    CEGEX_TOKEN_CLASS_NOT_DIGIT,      // \D
    CEGEX_TOKEN_CLASS_WHITESPACE,     // \s
    CEGEX_TOKEN_CLASS_NOT_WHITESPACE, // \S
    CEGEX_TOKEN_ANCHOR_START,         // ^
    CEGEX_TOKEN_ANCHOR_END,           // $
    CEGEX_TOKEN_ANCHOR_BOUNDARY,      // \b
    CEGEX_TOKEN_ANCHOR_NOT_BOUNDARY,  // \B
    CEGEX_TOKEN_ESCAPE_CHARACTER,
    CEGEX_TOKEN_ESCAPE_BELL,            // \a
    CEGEX_TOKEN_ESCAPE_FORM_FEED,       // \f
    CEGEX_TOKEN_ESCAPE_HORIZONTAL_TAB,  // \t
    CEGEX_TOKEN_ESCAPE_NEWLINE,         // \n
    CEGEX_TOKEN_ESCAPE_CARRIAGE_RETURN, // \r
    CEGEX_TOKEN_ESCAPE_VERTICAL_TAB,    // \v
    CEGEX_TOKEN_ESCAPE_OCTAL,           // \123 (up to three digits)
    CEGEX_TOKEN_ESCAPE_HEX,             // \x7F (up to two digits)
    CEGEX_TOKEN_STAR,
    CEGEX_TOKEN_PLUS,
    CEGEX_TOKEN_QUESTION_MARK,
    CEGEX_TOKEN_COLON,
    CEGEX_TOKEN_ALTERNATE,
    CEGEX_TOKEN_PAREN_OPEN,
    CEGEX_TOKEN_PAREN_CLOSE,
    CEGEX_TOKEN_SQUARE_OPEN,
    CEGEX_TOKEN_SQUARE_CLOSE,
    CEGEX_TOKEN_CURLY_OPEN,
    CEGEX_TOKEN_CURLY_CLOSE,
    CEGEX_TOKEN_COMMA
} cegex_token_type;

typedef struct cegex_token_text
{
    cegex_token_type type;
    const char* text;
} cegex_token_text;

static cegex_token_text token_texts[] = {
    { CEGEX_TOKEN_UNKNOWN, "CEGEX_TOKEN_UNKNOWN" },
    { CEGEX_TOKEN_CHARACTER, "CEGEX_TOKEN_CHARACTER" },
    { CEGEX_TOKEN_CLASS_ANY, "CEGEX_TOKEN_CLASS_ANY" },
    { CEGEX_TOKEN_CLASS_WORD, "CEGEX_TOKEN_CLASS_WORD" },
    { CEGEX_TOKEN_CLASS_NOT_WORD, "CEGEX_TOKEN_CLASS_NOT_WORD" },
    { CEGEX_TOKEN_CLASS_DIGIT, "CEGEX_TOKEN_CLASS_DIGIT" },
    { CEGEX_TOKEN_CLASS_NOT_DIGIT, "CEGEX_TOKEN_CLASS_NOT_DIGIT" },
    { CEGEX_TOKEN_CLASS_WHITESPACE, "CEGEX_TOKEN_CLASS_WHITESPACE" },
    { CEGEX_TOKEN_CLASS_NOT_WHITESPACE, "CEGEX_TOKEN_CLASS_NOT_WHITESPACE" },
    { CEGEX_TOKEN_ANCHOR_START, "CEGEX_TOKEN_ANCHOR_START" },
    { CEGEX_TOKEN_ANCHOR_END, "CEGEX_TOKEN_ANCHOR_END" },
    { CEGEX_TOKEN_ANCHOR_BOUNDARY, "CEGEX_TOKEN_ANCHOR_BOUNDARY" },
    { CEGEX_TOKEN_ANCHOR_NOT_BOUNDARY, "CEGEX_TOKEN_ANCHOR_NOT_BOUNDARY" },
    { CEGEX_TOKEN_ESCAPE_CHARACTER, "CEGEX_TOKEN_ESCAPE_CHARACTER" },
    { CEGEX_TOKEN_ESCAPE_BELL, "CEGEX_TOKEN_ESCAPE_BELL" },
    { CEGEX_TOKEN_ESCAPE_FORM_FEED, "CEGEX_TOKEN_ESCAPE_FORM_FEED" },
    { CEGEX_TOKEN_ESCAPE_HORIZONTAL_TAB, "CEGEX_TOKEN_ESCAPE_HORIZONTAL_TAB" },
    { CEGEX_TOKEN_ESCAPE_NEWLINE, "CEGEX_TOKEN_ESCAPE_NEWLINE" },
    { CEGEX_TOKEN_ESCAPE_CARRIAGE_RETURN, "CEGEX_TOKEN_ESCAPE_CARRIAGE_RETURN" },
    { CEGEX_TOKEN_ESCAPE_VERTICAL_TAB, "CEGEX_TOKEN_ESCAPE_VERTICAL_TAB" },
    { CEGEX_TOKEN_ESCAPE_OCTAL, "CEGEX_TOKEN_ESCAPE_OCTAL" },
    { CEGEX_TOKEN_ESCAPE_HEX, "CEGEX_TOKEN_ESCAPE_HEX" },
    { CEGEX_TOKEN_STAR, "CEGEX_TOKEN_STAR" },
    { CEGEX_TOKEN_PLUS, "CEGEX_TOKEN_PLUS" },
    { CEGEX_TOKEN_QUESTION_MARK, "CEGEX_TOKEN_QUESTION_MARK" },
    { CEGEX_TOKEN_COLON, "CEGEX_TOKEN_COLON" },
    { CEGEX_TOKEN_ALTERNATE, "CEGEX_TOKEN_ALTERNATE" },
    { CEGEX_TOKEN_PAREN_OPEN, "CEGEX_TOKEN_PAREN_OPEN" },
    { CEGEX_TOKEN_PAREN_CLOSE, "CEGEX_TOKEN_PAREN_CLOSE" },
    { CEGEX_TOKEN_SQUARE_OPEN, "CEGEX_TOKEN_SQUARE_OPEN" },
    { CEGEX_TOKEN_SQUARE_CLOSE, "CEGEX_TOKEN_SQUARE_CLOSE" },
    { CEGEX_TOKEN_CURLY_OPEN, "CEGEX_TOKEN_CURLY_OPEN" },
    { CEGEX_TOKEN_CURLY_CLOSE, "CEGEX_TOKEN_CURLY_CLOSE" },
    { CEGEX_TOKEN_COMMA, "CEGEX_TOKEN_COMMA" }
};

typedef struct cegex_token
{
    size_t start;
    size_t length;
    cegex_token_type type;
} cegex_token;

typedef ARRAY(cegex_token) cegex_token_array;
typedef ITERATOR(byte) byte_iterator;

typedef struct cegex_tokenizer
{
    byte_iterator iterator;

    size_t token_arrays_length;
    cegex_token_array token_arrays[CEGEX_TOKENIZER_TOKEN_ARRAYS_CAPACITY];
} cegex_tokenizer;

cegex_token_type cegex_token_type_from_char(char needle)
{
    switch(needle) {
    case '.':
        return CEGEX_TOKEN_CLASS_ANY;
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
    default:
        return CEGEX_TOKEN_CHARACTER;
    }
}

const char* cegex_token_str(cegex_token_type type)
{
    const size_t LENGTH = sizeof(token_texts) / sizeof(cegex_token_text);

    for(size_t i = 0; i < LENGTH; i++) {
        if(type == token_texts[i].type) {
            return token_texts[i].text;
        }
    }

    return "CEGEX_TOKEN_UNKNOWN";
}

cegex_status cegex_tokenizer_init(cegex_tokenizer* ctx)
{
    assert(ctx != NULL);
    ctx->token_arrays_length = 0;

    return CEGEX_OK;
}

cegex_token_array* cegex_tokenizer_active_array(cegex_tokenizer* ctx)
{
    if(ctx->token_arrays_length == 0)
        return NULL;

    return &ctx->token_arrays[ctx->token_arrays_length - 1];
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

            switch(current) {
                // Perl character classes
            case 'w':
                token.type = CEGEX_TOKEN_CLASS_WORD;
                break;
            case 'W':
                token.type = CEGEX_TOKEN_CLASS_NOT_WORD;
                break;
            case 'd':
                token.type = CEGEX_TOKEN_CLASS_DIGIT;
                break;
            case 'D':
                token.type = CEGEX_TOKEN_CLASS_NOT_DIGIT;
                break;
            case 's':
                token.type = CEGEX_TOKEN_CLASS_WHITESPACE;
                break;
            case 'S':
                token.type = CEGEX_TOKEN_CLASS_NOT_WHITESPACE;
                break;

                // Anchors
            case 'b':
                token.type = CEGEX_TOKEN_ANCHOR_BOUNDARY;
                break;
            case 'B':
                token.type = CEGEX_TOKEN_ANCHOR_NOT_BOUNDARY;
                break;

                // Supported special escape sequences
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
                    cegex_tokenizer_consume_digits(ctx, cegex_isodigit, 2);
                } else if(current == 'x') {
                    token.type = CEGEX_TOKEN_ESCAPE_HEX;
                    cegex_tokenizer_consume_digits(ctx, isxdigit, 2);
                } else {
                    token.type = CEGEX_TOKEN_ESCAPE_CHARACTER;
                }
            } break;
            }
        } else {
            token.type = cegex_token_type_from_char(current);
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

int main(void)
{
    cegex_tokenizer ctx = { 0 };
    cegex_tokenizer_init(&ctx);

    cegex_token_array tokens = { 0 };
    const char* expr = "\\xZ[(ab[de]|a)*c";
    if(CEGEX_OK != cegex_tokenizer_exec(&ctx, expr, 16, &tokens)) {
        printf("Error occured during tokenization\n");
        cegex_tokenizer_free(&ctx);
        return 1;
    }

    for(size_t i = 0; i < tokens.length; i++) {
        cegex_token tok = { 0 };
        ARRAY_NTH(&tokens, i, &tok);

        char buf[32];
        memcpy(buf, expr + tok.start, tok.length);
        buf[tok.length] = '\0';

        printf("(%zu): \"%s\" \t\t(%s)\n", i, buf, cegex_token_str(tok.type));
    }

    cegex_tokenizer_free(&ctx);
    return 0;
}

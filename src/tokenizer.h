#ifndef CEGEX_TOKENIZER_H
#define CEGEX_TOKENIZER_H

#include "common.h"
#include "util/collections.h"
#include <stdint.h>

#define TOKEN_CHARACTER                       0x01
#define TOKEN_NUMBER                          0x02
#define TOKEN_HEX_DIGIT                       0x04
#define TOKEN_OCT_DIGIT                       0x08
#define TOKEN_ESC                             0x10
#define TOKEN_PERL                            0x20
#define TOKEN_SPECIAL                         0x40

#define CEGEX_TOKENIZER_TOKEN_ARRAYS_CAPACITY 32

typedef enum cegex_token_type
{
    CEGEX_TOKEN_UNKNOWN = 0,
    CEGEX_TOKEN_CHARACTER,

    CEGEX_TOKEN_CLASS_DOT,            // .
    CEGEX_TOKEN_CLASS_WORD,           // \w
    CEGEX_TOKEN_CLASS_NOT_WORD,       // \W
    CEGEX_TOKEN_CLASS_DIGIT,          // \d
    CEGEX_TOKEN_CLASS_NOT_DIGIT,      // \D
    CEGEX_TOKEN_CLASS_WHITESPACE,     // \s
    CEGEX_TOKEN_CLASS_NOT_WHITESPACE, // \S

    CEGEX_TOKEN_ANCHOR_START,        // ^
    CEGEX_TOKEN_ANCHOR_END,          // $
    CEGEX_TOKEN_ANCHOR_BOUNDARY,     // \b
    CEGEX_TOKEN_ANCHOR_NOT_BOUNDARY, // \B

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
    CEGEX_TOKEN_MINUS,
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

typedef struct cegex_token
{
    size_t start;
    size_t length;

    uint8_t flags;
    cegex_token_type type;
} cegex_token;

typedef ARRAY(cegex_token) cegex_token_array;
typedef ITERATOR(cbyte) byte_iterator;

typedef struct cegex_tokenizer
{
    byte_iterator iterator;

    size_t token_arrays_length;
    cegex_token_array token_arrays[CEGEX_TOKENIZER_TOKEN_ARRAYS_CAPACITY];
} cegex_tokenizer;

cegex_status cegex_tokenizer_init(cegex_tokenizer* ctx);

cegex_status cegex_tokenizer_exec(cegex_tokenizer* ctx,
                                  const char* source,
                                  size_t n,
                                  cegex_token_array* out);

void cegex_tokenizer_free(cegex_tokenizer* ctx);

#endif
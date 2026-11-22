#include <stdio.h>

#include "./src/automata.h"
#include "./src/tokenizer.h"
#include "./src/util/debug.h"
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/**
 * Expr
 *  ::= Alternation;
 *
 * Alternation
 *  ::= Concat { "|" Concat };
 *
 * Concat
 *  ::= Quantified { Quantified };
 *
 * Quantified
 *  ::= Primary [ Quantifier ];
 *
 * Quantifier
 *  ::= ("*" | "+" | "?" | CountedQuantifier) [ "?" ];
 *
 * CountedQuantifier
 *  ::=     "{" Integer "}"
 *      |   "{" Integer "," Integer "}"
 *      |   "{" Integer "," "}";
 *
 * Primary
 *  ::=     Character
 *      |   CharacterClass
 *      |   EscapeSequence
 *      |   Boundary
 *      |   Group;
 *
 * Boundary
 *  ::=     "^"  | "$"
 *      |   "\b" | "\B";
 *
 * Group
 *  ::=     "("   Expr ")"
 *      |   "(?:" Expr ")";
 *
 * CharacterClass
 *  ::=     "."
 *      |   CharacterSet
 *      |   PerlCharacterClass;
 *
 * CharacterSet
 *  ::= "[" [ "^" ] CharacterSetElements "]";
 *
 * CharacterSetElements
 *  ::= CharacterSetElement { CharacterSetElement };
 *
 * CharacterSetElement
 *  ::=     CharacterSetRange
 *      |   CharacterSetAtom;
 *
 * CharacterSetRange
 *  ::= CharacterSetRangeRune  "-" CharacterSetRangeRune;
 *
 * CharacterSetRangeRune
 *  ::=     Character
 *      |   EscapeSequence;
 *
 * CharacterSetAtom
 *  ::=     Character
 *      |   EscapeSequence
 *      |   PerlCharacterClass;
 *
 * PerlCharacterClass
 *  :=      "\w" | "\W"
 *      |   "\d" | "\D"
 *      |   "\s" | "\S";
 *
 * EscapeSequence
 *  ::=     "\a" | "\f"
 *      |   "\t" | "\n"
 *      |   "\r" | "\v"
 *      |   "\x" HexDigit HexDigit
 *      |   "\x" "{" HexDigits "}"
 *      |   OctalEscapeSequence;
 *
 * OctalEscapeSequence
 *  ::= "\" OctalDigit [ OctalDigit [ OctalDigit ] ];
 *
 * OctalDigit
 *  ::= /[0-7]/;
 *
 * HexDigits
 *  ::= HexDigit { HexDigit };
 *
 * HexDigit
 *  ::= Digit | /[A-Fa-f]/;
 *
 * Digit
 *  ::= /[0-9]/;
 *
 * Character
 *  ::= /[ -~]/
 */

#define CEGEX_ASCII_CHAR_FIRST         32
#define CEGEX_ASCII_CHAR_LAST          126

#define CEGEX_PARSER_CHARSET_CAPACITY  256

#define PARSING_PRECEDENCE_NONE        0
#define PARSING_PRECEDENCE_ALTERNATION 10
#define PARSING_PRECEDENCE_CONCAT      20
#define PARSING_PRECEDENCE_QUANTIFIER  30
#define PARSING_PRECEDENCE_LAZY        40

typedef enum cegex_parse_node_type
{
    CEGEX_PARSE_NODE_UNKNOWN = 0,
    CEGEX_PARSE_NODE_CHARSET,

    CEGEX_PARSE_NODE_CONCAT,
    CEGEX_PARSE_NODE_ALTERNATE,

    CEGEX_PARSE_NODE_GROUP,

    CEGEX_PARSE_NODE_QUANTIFIER_PLUS,
    CEGEX_PARSE_NODE_QUANTIFIER_STAR,
    CEGEX_PARSE_NODE_QUANTIFIER_COUNTED,
    CEGEX_PARSE_NODE_QUANTIFIER_OPTIONAL,
    CEGEX_PARSE_NODE_QUANTIFIER_LAZY,

    CEGEX_PARSE_NODE_ANCHOR_START,
    CEGEX_PARSE_NODE_ANCHOR_END,
    CEGEX_PARSE_NODE_ANCHOR_WORD_BOUNDARY,
    CEGEX_PARSE_NODE_ANCHOR_NOT_WORD_BOUNDARY,
} cegex_parse_node_type;

typedef struct cegex_parse_node_binary
{
    struct cegex_parse_node* lhs;
    struct cegex_parse_node* rhs;
} cegex_parse_node_alternation, cegex_parse_node_concat;

typedef struct cegex_parse_node_unary
{
    struct cegex_parse_node* value;
} cegex_parse_node_quantified, cegex_parse_node_group;

typedef struct cegex_character_set
{
    bool negated;
    struct
    {
        bool word : 1;
        bool not_word : 1;
        bool digit : 1;
        bool not_digit : 1;
        bool space : 1;
        bool not_space : 1;
    } perl;

    size_t num_characters;
    cegex_uchar characters[CEGEX_PARSER_CHARSET_CAPACITY];
} cegex_character_set;

typedef enum cegex_counted_quantifier_type
{
    CEGEX_COUNTED_QUANTIFIER_EXACT,
    CEGEX_COUNTED_QUANTIFIER_MIN,
    CEGEX_COUNTED_QUANTIFIER_MIN_MAX,
} cegex_counted_quantifier_type;

typedef struct cegex_parse_node
{
    cegex_parse_node_type type;

    union
    {
        struct
        {
            bool is_capturing;
        } group;

        struct
        {
            cegex_counted_quantifier_type type;
            size_t count;
            size_t min;
            size_t max;
        } counted;
    } data;

    union
    {
        struct cegex_character_set set;
        struct cegex_parse_node_unary unary;
        struct cegex_parse_node_binary binary;
    } as;
} cegex_parse_node;

typedef ITERATOR(cegex_token) cegex_token_iterator;
typedef struct cegex_parsed_expr
{
    cegex_parse_node* root;
    cegex_tokenized_expr tokenized;
} cegex_parsed_expr;

typedef struct cegex_parser
{
    cegex_token_iterator token_iterator;

    size_t num_parsed;
    cegex_parsed_expr parsed[CEGEX_EXPRESSION_CAPACITY];
} cegex_parser;

bool cegex_token_starts_primary(cegex_token tok)
{
    if(tok.type == CEGEX_TOKEN_PLUS ||
       tok.type == CEGEX_TOKEN_QUESTION_MARK ||
       tok.type == CEGEX_TOKEN_STAR)
        return false;

    return true;
}

bool cegex_token_starts_quantifier(cegex_token tok)
{
    switch(tok.type) {
    case CEGEX_TOKEN_PLUS:
    case CEGEX_TOKEN_CURLY_OPEN:
    case CEGEX_TOKEN_STAR:
    case CEGEX_TOKEN_QUESTION_MARK:
        return true;

    default:
        return false;
    }
}

void cegex_parse_node_free(cegex_parse_node* node)
{
    if(node == NULL)
        return;

    switch(node->type) {
    case CEGEX_PARSE_NODE_CONCAT:
    case CEGEX_PARSE_NODE_ALTERNATE:
        cegex_parse_node_free(node->as.binary.lhs);
        cegex_parse_node_free(node->as.binary.rhs);
        break;

    case CEGEX_PARSE_NODE_QUANTIFIER_PLUS:
    case CEGEX_PARSE_NODE_QUANTIFIER_STAR:
    case CEGEX_PARSE_NODE_QUANTIFIER_COUNTED:
    case CEGEX_PARSE_NODE_QUANTIFIER_OPTIONAL:
    case CEGEX_PARSE_NODE_QUANTIFIER_LAZY:
    case CEGEX_PARSE_NODE_GROUP:
        cegex_parse_node_free(node->as.unary.value);
        break;
    default:
        break;
    }

    free(node);
    node = NULL;
}

cegex_parse_node* cegex_parse_node_empty(cegex_parse_node_type type)
{
    cegex_parse_node* node = malloc(sizeof(cegex_parse_node));
    node->type = type;

    return node;
}

cegex_parse_node* cegex_parse_node_binary(
    cegex_parse_node_type type,
    cegex_parse_node* lhs,
    cegex_parse_node* rhs)
{
    cegex_parse_node* cat = malloc(sizeof(cegex_parse_node));
    cat->type = type;
    cat->as.binary.lhs = lhs;
    cat->as.binary.rhs = rhs;

    return cat;
}

cegex_parse_node* cegex_parse_node_unary(cegex_parse_node_type type,
                                         cegex_parse_node* lhs)
{
    cegex_parse_node* node = malloc(sizeof(cegex_parse_node));
    node->type = type;
    node->as.unary.value = lhs;

    return node;
}

#define CEGEX_CHARSET(neg, num, chars) \
    ((cegex_character_set){            \
        .negated = neg,                \
        .num_characters = num,         \
        .characters = chars })

#define CEGEX_CHARSET_SINGLE(c)      CEGEX_CHARSET(false, 1, { (c) })

#define CEGEX_CHARSET_PERL_WORD      CEGEX_CHARSET(false, 63, "ABCDEFGHIJKLMNOPQRSTUVWXYZ" \
                                                              "abcdefghijklmnopqrstuvwxyz" \
                                                              "0123456789_")

#define CEGEX_CHARSET_PERL_NOT_WORD  CEGEX_CHARSET(true, 63, "ABCDEFGHIJKLMNOPQRSTUVWXYZ" \
                                                             "abcdefghijklmnopqrstuvwxyz" \
                                                             "0123456789_")

#define CEGEX_CHARSET_PERL_DIGIT     CEGEX_CHARSET(false, 10, "0123456789")
#define CEGEX_CHARSET_PERL_NOT_DIGIT CEGEX_CHARSET(true, 10, "0123456789")

#define CEGEX_CHARSET_PERL_SPACE     CEGEX_CHARSET(false, 3, " \n\t")
#define CEGEX_CHARSET_PERL_NOT_SPACE CEGEX_CHARSET(true, 3, " \n\t")

cegex_status cegex_character_set_create(const char* characters,
                                        size_t length,
                                        bool negated,
                                        cegex_character_set* out)
{
    if(length >= CEGEX_PARSER_CHARSET_CAPACITY) {
        return CEGEX_ERR_LIMIT;
    }

    cegex_character_set set;
    set.negated = negated;
    set.num_characters = length;
    memcpy(set.characters, characters, length);

    *out = set;

    return CEGEX_OK;
}

cegex_status cegex_character_set_add(cegex_character_set* set,
                                     cegex_uchar character)
{
    if(set->num_characters >= CEGEX_PARSER_CHARSET_CAPACITY) {
        return CEGEX_ERR_LIMIT;
    }

    cegex_uchar* characters = set->characters;
    if(!array_contains((void**) &characters, set->num_characters, sizeof(cegex_uchar), (void*) &character, (comparator) cegex_uchar_eq)) {
        set->characters[set->num_characters++] = character;
    }

    return CEGEX_OK;
}

cegex_parse_node* cegex_parse_node_charset(
    cegex_parse_node_type type,
    cegex_character_set set)
{
    cegex_parse_node* node = malloc(sizeof(cegex_parse_node));
    node->type = type;
    node->as.set = set;

    return node;
}

cegex_parsed_expr* cegex_parser_working_expr(cegex_parser* ctx)
{
    assert(ctx != NULL);

    size_t index = ctx->num_parsed;
    if(ctx->num_parsed >= CEGEX_EXPRESSION_CAPACITY) {
        index = ctx->num_parsed - 1;
    }

    return &ctx->parsed[index];
}

cegex_status cegex_parser_expect_tok_typ(cegex_parser* ctx,
                                         cegex_token_type type)
{
    assert(ctx != NULL);

    cegex_token tok;
    if(CEGEX_OK != ITERATOR_CURRENT(&ctx->token_iterator, &tok))
        return CEGEX_ERR_SYNTAX;

    if(type == tok.type) {
        ITERATOR_NEXT(&ctx->token_iterator);
        return CEGEX_OK;
    }

    return CEGEX_ERR_SYNTAX;
}

cegex_status cegex_parser_expect_tok_flg(cegex_parser* ctx,
                                         cegex_token_flags flags)
{
    assert(ctx != NULL);

    cegex_token tok;
    if(CEGEX_OK != ITERATOR_CURRENT(&ctx->token_iterator, &tok))
        return CEGEX_ERR_SYNTAX;

    if((tok.flags & flags) > 0) {
        ITERATOR_NEXT(&ctx->token_iterator);
        return CEGEX_OK;
    }

    return CEGEX_ERR_SYNTAX;
}

bool cegex_token_is_charset_range_rune(cegex_token tok)
{
    if(tok.type == CEGEX_TOKEN_SQUARE_CLOSE || tok.flags & TOKEN_PERL)
        return false;

    return true;
}

cegex_character_set cegex_perl_set_by_tok_type(cegex_token_type type)
{
    switch(type) {
    case CEGEX_TOKEN_CLASS_NOT_WORD:
        return CEGEX_CHARSET_PERL_NOT_WORD;
    case CEGEX_TOKEN_CLASS_WORD:
        return CEGEX_CHARSET_PERL_WORD;
    case CEGEX_TOKEN_CLASS_DIGIT:
        return CEGEX_CHARSET_PERL_DIGIT;
    case CEGEX_TOKEN_CLASS_NOT_DIGIT:
        return CEGEX_CHARSET_PERL_NOT_DIGIT;
    case CEGEX_TOKEN_CLASS_WHITESPACE:
        return CEGEX_CHARSET_PERL_SPACE;
    case CEGEX_TOKEN_CLASS_NOT_WHITESPACE:
        return CEGEX_CHARSET_PERL_NOT_SPACE;

    default:
        assert(false);
        return (cegex_character_set){ 0 };
    }
}

cegex_uchar cegex_parser_tok_charcode(cegex_parser* ctx,
                                      cegex_token tok)
{
    assert(ctx != NULL);
    cegex_parsed_expr* working = cegex_parser_working_expr(ctx);

    if(tok.flags & TOKEN_ESC) {
        switch(tok.type) {
        case CEGEX_TOKEN_ESCAPE_HEX: {
            return 0;
        }

        case CEGEX_TOKEN_ESCAPE_OCTAL: {
            return 0;
        };

        case CEGEX_TOKEN_ESCAPE_BELL:
            return '\a';
        case CEGEX_TOKEN_ESCAPE_CARRIAGE_RETURN:
            return '\r';
        case CEGEX_TOKEN_ESCAPE_FORM_FEED:
            return '\f';
        case CEGEX_TOKEN_ESCAPE_NEWLINE:
            return '\n';
        case CEGEX_TOKEN_ESCAPE_HORIZONTAL_TAB:
            return '\t';
        case CEGEX_TOKEN_ESCAPE_VERTICAL_TAB:
            return '\v';

        default: {
            assert(tok.length == 2);
            return working->tokenized.source[tok.start + 1];
        };
        }
    } else {
        assert(tok.length == 1);
        return working->tokenized.source[tok.start];
    }
}

cegex_status cegex_character_set_add_range(cegex_character_set* set,
                                           cegex_uchar low,
                                           cegex_uchar high)
{
    cegex_status status = CEGEX_OK;
    cegex_uchar* set_characters = set->characters;
    for(cegex_uchar curr = low; curr <= high; curr += 1) {
        if(array_contains((void**) &set_characters, set->num_characters, sizeof(cegex_uchar), &curr, (comparator) cegex_uchar_eq))
            continue;

        CEGEX_TRY(cegex_character_set_add(set, curr));
    }

fail:
    return status;
}

cegex_status cegex_character_set_add_subset(cegex_character_set* set,
                                            cegex_character_set subset)
{
    cegex_status status = CEGEX_OK;
    if(subset.negated) {
        cegex_uchar* subset_characters = subset.characters;
        for(cegex_uchar curr = CEGEX_ASCII_CHAR_FIRST; curr <= CEGEX_ASCII_CHAR_LAST; curr += 1) {
            if(array_contains((void**) &subset_characters, subset.num_characters, sizeof(cegex_uchar), &curr, (comparator) cegex_uchar_eq))
                continue;

            CEGEX_TRY(cegex_character_set_add(set, curr));
        }
    } else {
        for(size_t i = 0; i < subset.num_characters; i++) {
            CEGEX_TRY(cegex_character_set_add(set, subset.characters[i]));
        }
    }

fail:
    return status;
}

/**
 * CharacterSet
 *  ::= "[" [ "^" ] CharacterSetElements "]";
 */
cegex_status cegex_parser_character_set(cegex_parser* ctx,
                                        cegex_parse_node** out)
{
    cegex_status status = CEGEX_OK;

    cegex_character_set set = { 0 };
    CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_SQUARE_OPEN));

    cegex_token current = { 0 }, previous = { 0 };
    ITERATOR_CURRENT(&ctx->token_iterator, &current);
    if(current.type == CEGEX_TOKEN_HAT) {
        set.negated = true;
        CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_HAT));
    }

    cegex_parsed_expr* working = cegex_parser_working_expr(ctx);
    ITERATOR_CURRENT(&ctx->token_iterator, &current);
    while(!ITERATOR_EMPTY(&ctx->token_iterator) && current.type != CEGEX_TOKEN_SQUARE_CLOSE) {

        cegex_token peeked;
        if(CEGEX_OK == ITERATOR_PEEK(&ctx->token_iterator, &peeked)) {
            if(current.type == CEGEX_TOKEN_MINUS &&
               previous.type != CEGEX_TOKEN_UNKNOWN &&
               cegex_token_is_charset_range_rune(previous) &&
               cegex_token_is_charset_range_rune(peeked)) {

                cegex_uchar low = cegex_parser_tok_charcode(ctx, previous);
                cegex_uchar high = cegex_parser_tok_charcode(ctx, peeked);
                if(low > high) {
                    status = CEGEX_ERR_SYNTAX;
                    goto fail;
                }

                // Have to reset the previous token here, as [a-b-c] would otherwise create two seperate ranges
                previous.type = CEGEX_TOKEN_UNKNOWN;
                CEGEX_TRY(cegex_character_set_add_range(&set, low, high));
                CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_MINUS));
                goto next;
            }
        }

        if(current.flags & TOKEN_ESC) {
            if(current.flags & TOKEN_PERL) {
                CEGEX_TRY(cegex_character_set_add_subset(&set, cegex_perl_set_by_tok_type(current.type)));
            } else {
                CEGEX_TRY(cegex_character_set_add(&set, cegex_parser_tok_charcode(ctx, current)));
            }
        } else {
            assert(current.length == 1);
            CEGEX_TRY(cegex_character_set_add(&set, working->tokenized.source[current.start]));
        }

        previous = current;
    next:
        ITERATOR_NEXT(&ctx->token_iterator);
        ITERATOR_CURRENT(&ctx->token_iterator, &current);
    }

    CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_SQUARE_CLOSE));

    if(out != NULL) {
        *out = cegex_parse_node_charset(CEGEX_PARSE_NODE_CHARSET, set);
    }

    return status;

fail:
    return status;
}

/**
 * Primary
 *  ::=     Character
 *      |   CharacterClass
 *      |   EscapeSequence
 *      |   Boundary
 *      |   Group;
 */
cegex_status cegex_parser_primary(cegex_parser* ctx,
                                  cegex_parse_node** out)
{
    cegex_status status = CEGEX_OK;

    cegex_token current;
    if(CEGEX_OK != ITERATOR_CURRENT(&ctx->token_iterator, &current))
        return CEGEX_ERR_SYNTAX;

    if(!cegex_token_starts_primary(current))
        return CEGEX_ERR_SYNTAX;

    cegex_parsed_expr* working = cegex_parser_working_expr(ctx);
    switch(current.type) {

    case CEGEX_TOKEN_CHARACTER:
        CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CHARACTER));
        *out = cegex_parse_node_charset(
            CEGEX_PARSE_NODE_CHARSET,
            CEGEX_CHARSET_SINGLE(working->tokenized.source[current.start]));
        break;

    case CEGEX_TOKEN_CLASS_DOT:
        CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CLASS_DOT));
        *out = cegex_parse_node_charset(
            CEGEX_PARSE_NODE_CHARSET,
            (cegex_character_set){
                .negated = true,
                .num_characters = 2,
                .characters = { '\n', '\r' } });
        break;

    case CEGEX_TOKEN_SQUARE_OPEN: {
        CEGEX_TRY(cegex_parser_character_set(ctx, out));
    } break;

    case CEGEX_TOKEN_CLASS_NOT_WORD:
    case CEGEX_TOKEN_CLASS_WORD: {
        CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, TOKEN_PERL));
        bool negated = current.type == CEGEX_TOKEN_CLASS_NOT_WORD;

        *out = cegex_parse_node_charset(
            CEGEX_PARSE_NODE_CHARSET,
            negated ? CEGEX_CHARSET_PERL_NOT_WORD : CEGEX_CHARSET_PERL_WORD);

    } break;

    case CEGEX_TOKEN_CLASS_DIGIT:
    case CEGEX_TOKEN_CLASS_NOT_DIGIT: {
        CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, TOKEN_PERL));
        bool negated = current.type == CEGEX_TOKEN_CLASS_NOT_DIGIT;
        *out = cegex_parse_node_charset(
            CEGEX_PARSE_NODE_CHARSET,
            negated ? CEGEX_CHARSET_PERL_NOT_DIGIT : CEGEX_CHARSET_PERL_DIGIT);

    } break;

    case CEGEX_TOKEN_CLASS_WHITESPACE:
    case CEGEX_TOKEN_CLASS_NOT_WHITESPACE: {
        CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, TOKEN_PERL));
        bool negated = current.type == CEGEX_TOKEN_CLASS_NOT_WHITESPACE;

        *out = cegex_parse_node_charset(
            CEGEX_PARSE_NODE_CHARSET,
            negated ? CEGEX_CHARSET_PERL_NOT_SPACE : CEGEX_CHARSET_PERL_SPACE);

    } break;

    case CEGEX_TOKEN_HAT:
    case CEGEX_TOKEN_DOLLAR:
    case CEGEX_TOKEN_ANCHOR_BOUNDARY:
    case CEGEX_TOKEN_ANCHOR_NOT_BOUNDARY: {
        cegex_parse_node_type node_type = CEGEX_PARSE_NODE_ANCHOR_START;
        node_type = (current.type == CEGEX_TOKEN_DOLLAR) ? CEGEX_PARSE_NODE_ANCHOR_END : node_type;
        node_type = (current.type == CEGEX_TOKEN_ANCHOR_BOUNDARY) ? CEGEX_PARSE_NODE_ANCHOR_WORD_BOUNDARY : node_type;
        node_type = (current.type == CEGEX_TOKEN_ANCHOR_NOT_BOUNDARY) ? CEGEX_PARSE_NODE_ANCHOR_NOT_WORD_BOUNDARY : node_type;

        CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, TOKEN_SPECIAL));
        *out = cegex_parse_node_empty(node_type);
    } break;

    case CEGEX_TOKEN_PAREN_OPEN: {
        // group
    } break;

    default: {
        if(current.flags & TOKEN_ESC) {
            CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, TOKEN_ESC));
            *out = cegex_parse_node_charset(
                CEGEX_PARSE_NODE_CHARSET,
                CEGEX_CHARSET_SINGLE(cegex_parser_tok_charcode(ctx, current)));
        } else {
            CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, TOKEN_SPECIAL));
            *out = cegex_parse_node_charset(
                CEGEX_PARSE_NODE_CHARSET,
                CEGEX_CHARSET_SINGLE(working->tokenized.source[current.start]));
        }
    } break;
    }

fail:
    return status;
}

#define CEGEX_INTEGER_BUFFER_LENGTH 32

cegex_status cegex_parser_integer(cegex_parser* ctx,
                                  size_t* out)
{
    cegex_status status = CEGEX_OK;

    size_t num_numbers = 0;
    cegex_uchar buffer[CEGEX_INTEGER_BUFFER_LENGTH];
    while(!ITERATOR_EMPTY(&ctx->token_iterator)) {
        if(num_numbers >= CEGEX_INTEGER_BUFFER_LENGTH - 1) {
            status = CEGEX_ERR_LIMIT;
            break;
        }

        cegex_token current;
        ITERATOR_CURRENT(&ctx->token_iterator, &current);

        if(!(current.flags & TOKEN_NUMBER))
            break;

        buffer[num_numbers++] = cegex_parser_tok_charcode(ctx, current);
        ITERATOR_NEXT(&ctx->token_iterator);
    }

    if(num_numbers == 0)
        return CEGEX_ERR_SYNTAX;

    buffer[num_numbers] = '\0';

    char* end;
    *out = ((size_t) strtoll((const char*) buffer, &end, 10));

    return status;
}

void cegex_parser_skip_whitespace(cegex_parser* ctx)
{
    assert(ctx != NULL);

    cegex_token current;
    while(!ITERATOR_EMPTY(&ctx->token_iterator)) {
        ITERATOR_CURRENT(&ctx->token_iterator, &current);
        if(current.flags & TOKEN_WHITESPACE) {
            ITERATOR_NEXT(&ctx->token_iterator);
            continue;
        } else {
            break;
        }
    }
}

/**
 * CountedQuantifier
 *  ::= "{" Integer "}"
 *   |  "{" Integer "," Integer "}"
 *   |  "{" Integer "," "}"
 */
cegex_status cegex_parser_counted_quantifier(cegex_parser* ctx,
                                             cegex_parse_node** out)
{
    assert(ctx != NULL);
    assert(out != NULL);
    assert(*out != NULL);

    size_t min = 0, max = 0;
    cegex_status status = CEGEX_OK;
    cegex_counted_quantifier_type type;

    // "{"
    CEGEX_TRY(
        cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CURLY_OPEN));

    cegex_parser_skip_whitespace(ctx);
    // Integer
    if(CEGEX_OK != cegex_parser_integer(ctx, &min)) {
        status = CEGEX_ERR_UNEXPECTED;
        goto fail;
    }

    cegex_parser_skip_whitespace(ctx);
    // "}"
    if(CEGEX_OK == cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CURLY_CLOSE)) {
        type = CEGEX_COUNTED_QUANTIFIER_EXACT;
    } else {
        cegex_parser_skip_whitespace(ctx);
        // ","
        if(CEGEX_OK != cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_COMMA)) {
            status = CEGEX_ERR_UNEXPECTED;
            goto fail;
        }

        cegex_parser_skip_whitespace(ctx);
        cegex_token current;
        if(CEGEX_OK != ITERATOR_CURRENT(&ctx->token_iterator, &current)) {
            status = CEGEX_ERR_UNEXPECTED;
            goto fail;
        }

        if(current.flags & TOKEN_NUMBER) {
            // Integer
            if(CEGEX_OK != cegex_parser_integer(ctx, &max)) {
                status = CEGEX_ERR_UNEXPECTED;
                goto fail;
            }

            cegex_parser_skip_whitespace(ctx);
            // "}"
            if(CEGEX_OK != cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CURLY_CLOSE)) {
                status = CEGEX_ERR_UNEXPECTED;
                goto fail;
            }

            if(min > max) {
                status = CEGEX_ERR_SYNTAX;
                goto fail;
            }

            type = CEGEX_COUNTED_QUANTIFIER_MIN_MAX;
        } else {
            cegex_parser_skip_whitespace(ctx);
            // "}"
            if(CEGEX_OK != cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CURLY_CLOSE)) {
                status = CEGEX_ERR_UNEXPECTED;
                goto fail;
            }

            type = CEGEX_COUNTED_QUANTIFIER_MIN;
        }
    }

    cegex_parse_node* node = cegex_parse_node_unary(CEGEX_PARSE_NODE_QUANTIFIER_COUNTED, *out);
    node->data.counted.type = type;

    switch(type) {
    case CEGEX_COUNTED_QUANTIFIER_EXACT:
        node->data.counted.count = min;
        break;

    case CEGEX_COUNTED_QUANTIFIER_MIN_MAX:
        node->data.counted.min = min;
        node->data.counted.max = max;
        break;

    case CEGEX_COUNTED_QUANTIFIER_MIN:
        node->data.counted.min = min;
        break;
    }

    *out = node;
    return CEGEX_OK;

fail:
    return status;
}

/**
 * Quantifier
 *  ::= ("*" | "+" | "?" | CountedQuantifier) [ "?" ];
 */
cegex_status cegex_parser_quantifier(cegex_parser* ctx,
                                     cegex_parse_node** out)
{
    assert(ctx != NULL);
    assert(out != NULL);
    assert(*out != NULL);
    cegex_status status = CEGEX_OK;

    cegex_token current;
    CEGEX_TRY(
        ITERATOR_CURRENT(&ctx->token_iterator, &current));

    if(current.type == CEGEX_TOKEN_CURLY_OPEN) {
        const size_t saved = ctx->token_iterator.index;
        if(CEGEX_ERR_SYNTAX == (status = cegex_parser_counted_quantifier(ctx, out))) {
            goto fail;
        }

        if(CEGEX_ERR_UNEXPECTED == status) {
            ctx->token_iterator.index = saved;
            goto fail;
        }
    } else {
        cegex_parse_node_type node_type;
        switch(current.type) {
        case CEGEX_TOKEN_STAR:
            node_type = CEGEX_PARSE_NODE_QUANTIFIER_STAR;
            cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_STAR);
            break;

        case CEGEX_TOKEN_PLUS:
            node_type = CEGEX_PARSE_NODE_QUANTIFIER_PLUS;
            cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_PLUS);
            break;

        case CEGEX_TOKEN_QUESTION_MARK:
            node_type = CEGEX_PARSE_NODE_QUANTIFIER_OPTIONAL;
            cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_QUESTION_MARK);
            break;

        default:
            status = CEGEX_ERR_SYNTAX;
            goto fail;
        }

        cegex_parse_node* node = cegex_parse_node_unary(node_type, *out);
        *out = node;
    }

    if(CEGEX_OK == ITERATOR_CURRENT(&ctx->token_iterator, &current) &&
       current.type == CEGEX_TOKEN_QUESTION_MARK) {

        CEGEX_TRY(
            cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_QUESTION_MARK));

        cegex_parse_node* node = cegex_parse_node_unary(CEGEX_PARSE_NODE_QUANTIFIER_LAZY, *out);
        *out = node;
    }

    return CEGEX_OK;

fail:
    return status;
}

static bool cegex_parse_node_is_quantifier(const cegex_parse_node* node)
{
    switch(node->type) {
    case CEGEX_PARSE_NODE_QUANTIFIER_COUNTED:
    case CEGEX_PARSE_NODE_QUANTIFIER_LAZY:
    case CEGEX_PARSE_NODE_QUANTIFIER_OPTIONAL:
    case CEGEX_PARSE_NODE_QUANTIFIER_PLUS:
    case CEGEX_PARSE_NODE_QUANTIFIER_STAR:
        return true;

    default:
        return false;
    }
}

cegex_status cegex_parser_expr(cegex_parser* ctx,
                               uint32_t precedence,
                               cegex_parse_node** out)
{
    cegex_status status = CEGEX_OK;

    cegex_parse_node* lhs = NULL;
    CEGEX_TRY(cegex_parser_primary(ctx, &lhs));

    while(!ITERATOR_EMPTY(&ctx->token_iterator)) {
        cegex_token current;
        CEGEX_TRY(
            ITERATOR_CURRENT(&ctx->token_iterator, &current));

        if(cegex_token_starts_quantifier(current)) {
            if(cegex_parse_node_is_quantifier(lhs)) {
                // Cannot quantifiy an already quantified node
                status = CEGEX_ERR_SYNTAX;
                goto fail;
            }

            if(CEGEX_OK == (status = cegex_parser_quantifier(ctx, &lhs))) {
                continue;
            } else if(CEGEX_ERR_UNEXPECTED != status) {
                // Due to '{' starting both a primary and a quantifier expression, 'cegex_parser_quantifier'
                // will return 'CEGEX_ERR_UNEXPECTED' when it tried to parse a counted quantifier and failed,
                // this "error" is recoverable, however, anything else is a reason to abort the parsing process.
                goto fail;
            }
        }

        if(current.type == CEGEX_TOKEN_ALTERNATE) {
            if(precedence > PARSING_PRECEDENCE_ALTERNATION)
                break;

            CEGEX_TRY(
                cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_ALTERNATE));

            cegex_parse_node* rhs = NULL;
            CEGEX_TRY(
                cegex_parser_expr(ctx, PARSING_PRECEDENCE_ALTERNATION + 1, &rhs));

            lhs = cegex_parse_node_binary(CEGEX_PARSE_NODE_ALTERNATE, lhs, rhs);
        } else if(cegex_token_starts_primary(current)) {
            // Implicit concatination without an infix operator
            if(precedence > PARSING_PRECEDENCE_CONCAT)
                break;

            cegex_parse_node* rhs = NULL;
            CEGEX_TRY(
                cegex_parser_expr(ctx, PARSING_PRECEDENCE_CONCAT + 1, &rhs));

            lhs = cegex_parse_node_binary(CEGEX_PARSE_NODE_CONCAT, lhs, rhs);
        } else {
            status = CEGEX_ERR_SYNTAX;
            goto fail;
        }
    }

    *out = lhs;
    return CEGEX_OK;

fail:
    cegex_parse_node_free(lhs);
    out = NULL;

    return status;
}

cegex_status cegex_parser_exec(cegex_parser* ctx,
                               cegex_tokenized_expr tokenized,
                               cegex_parse_node** out)
{
    assert(ctx != NULL);
    assert(tokenized.tokens.length > 0);

    cegex_status status = CEGEX_OK;
    if(ctx->num_parsed >= CEGEX_EXPRESSION_CAPACITY) {
        status = CEGEX_ERR_LIMIT;
        goto fail;
    }

    ITERATOR_INIT(&ctx->token_iterator, tokenized.tokens.elements, tokenized.tokens.length);
    ctx->parsed[ctx->num_parsed].tokenized = tokenized;

    CEGEX_TRY(cegex_parser_expr(ctx, PARSING_PRECEDENCE_NONE, &ctx->parsed[ctx->num_parsed].root));
    if(out != NULL) {
        *out = ctx->parsed[ctx->num_parsed].root;
    }

    ctx->num_parsed += 1;

fail:
    return status;
}

/***************************************** */
#define TREE_BRANCH "+-- "
#define TREE_LAST   "`-- "
#define TREE_PIPE   "|   "
#define TREE_SPACE  "    "

static const char* cegex_parse_node_type_name(cegex_parse_node_type type)
{
    switch(type) {
    case CEGEX_PARSE_NODE_UNKNOWN:
        return "UNKNOWN";
    case CEGEX_PARSE_NODE_CHARSET:
        return "CHARSET";
    case CEGEX_PARSE_NODE_CONCAT:
        return "CONCAT";
    case CEGEX_PARSE_NODE_ALTERNATE:
        return "ALTERNATE";
    case CEGEX_PARSE_NODE_GROUP:
        return "GROUP";
    case CEGEX_PARSE_NODE_QUANTIFIER_PLUS:
        return "PLUS";
    case CEGEX_PARSE_NODE_QUANTIFIER_STAR:
        return "STAR";
    case CEGEX_PARSE_NODE_QUANTIFIER_COUNTED:
        return "COUNTED";
    case CEGEX_PARSE_NODE_QUANTIFIER_OPTIONAL:
        return "OPTIONAL";
    case CEGEX_PARSE_NODE_QUANTIFIER_LAZY:
        return "LAZY";
    case CEGEX_PARSE_NODE_ANCHOR_START:
        return "ANCHOR_START";
    case CEGEX_PARSE_NODE_ANCHOR_END:
        return "ANCHOR_END";
    case CEGEX_PARSE_NODE_ANCHOR_WORD_BOUNDARY:
        return "WORD_BOUNDARY";
    case CEGEX_PARSE_NODE_ANCHOR_NOT_WORD_BOUNDARY:
        return "NOT_WORD_BOUNDARY";
    }

    return "INVALID";
}

static bool cegex_parse_node_is_unary(cegex_parse_node_type type)
{
    return type == CEGEX_PARSE_NODE_GROUP ||
           type == CEGEX_PARSE_NODE_QUANTIFIER_PLUS ||
           type == CEGEX_PARSE_NODE_QUANTIFIER_STAR ||
           type == CEGEX_PARSE_NODE_QUANTIFIER_COUNTED ||
           type == CEGEX_PARSE_NODE_QUANTIFIER_OPTIONAL ||
           type == CEGEX_PARSE_NODE_QUANTIFIER_LAZY;
}

static bool cegex_parse_node_is_binary(cegex_parse_node_type type)
{
    return type == CEGEX_PARSE_NODE_CONCAT ||
           type == CEGEX_PARSE_NODE_ALTERNATE;
}

static void cegex_print_charset(cegex_character_set* set)
{
    printf("negated=%s, characters=[ ",
           set->negated ? "true" : "false");

    for(size_t i = 0; i < set->num_characters; i++) {
        cegex_uchar current = set->characters[i];
        switch(current) {
        case ' ':
            printf("<space>");
            break;
        case '\a':
            printf("\\a");
            break;

        case '\f':
            printf("\\f");
            break;

        case '\r':
            printf("\\r");
            break;

        case '\v':
            printf("\\v");
            break;

        case '\n':
            printf("\\n");
            break;

        case '\t':
            printf("\\t");
            break;

        default:
            printf("%c", (char) current);
            break;
        }

        if(i + 1 < set->num_characters) {
            printf(",");
        }
    }

    printf(" ]");
}

static void cegex_parse_node_print_impl(
    cegex_parse_node* node,
    const char* prefix,
    bool is_last,
    bool is_root)
{
    if(!node) {
        printf("%s%s<null>\n", prefix, is_last ? TREE_LAST : TREE_BRANCH);
        return;
    }

    printf("%s", prefix);
    if(!is_root)
        printf("%s", is_last ? TREE_LAST : TREE_BRANCH);

    printf("%s", cegex_parse_node_type_name(node->type));

    switch(node->type) {
    case CEGEX_PARSE_NODE_CHARSET:
        printf(" <");
        cegex_print_charset(&node->as.set);
        printf(">");
        break;

    case CEGEX_PARSE_NODE_GROUP:
        printf(" <capturing=%s>",
               node->data.group.is_capturing ? "true" : "false");
        break;

    case CEGEX_PARSE_NODE_QUANTIFIER_COUNTED:
        printf(" [");

        if(node->data.counted.type == CEGEX_COUNTED_QUANTIFIER_EXACT) {
            printf("%zu", node->data.counted.count);
        } else if(node->data.counted.type == CEGEX_COUNTED_QUANTIFIER_MIN_MAX) {
            printf("%zu, %zu",
                   node->data.counted.min,
                   node->data.counted.max);
        } else {
            printf("%zu, ...",
                   node->data.counted.min);
        }

        printf("]");
        break;

    default:
        break;
    }

    printf("\n");

    char child_prefix[512];
    snprintf(child_prefix, sizeof(child_prefix), "%s%s",
             prefix,
             is_root ? "" : (is_last ? TREE_SPACE : TREE_PIPE));

    if(cegex_parse_node_is_unary(node->type)) {
        cegex_parse_node_print_impl(
            node->as.unary.value,
            child_prefix,
            true,
            false);
    } else if(cegex_parse_node_is_binary(node->type)) {
        cegex_parse_node_print_impl(
            node->as.binary.lhs,
            child_prefix,
            false,
            false);

        cegex_parse_node_print_impl(
            node->as.binary.rhs,
            child_prefix,
            true,
            false);
    }
}

void cegex_parse_node_print(cegex_parse_node* root)
{
    if(!root) {
        printf("<empty tree>\n");
        return;
    }

    cegex_parse_node_print_impl(root, " ", true, true);
}
/***************************************** */

int main(void)
{
    cegex_status status = CEGEX_OK;

    cegex_parse_node* root = NULL;
    cegex_tokenizer tokenizer = { 0 };
    cegex_tokenized_expr tokenized = { 0 };

    const char* expr = "abc*?";
    if((status = cegex_tokenizer_exec(&tokenizer, expr, strlen(expr), &tokenized)) != CEGEX_OK) {
        printf("Error occured during tokenization\n");
        goto free;
    }

    cegex_parser parser = { 0 };
    if((status = cegex_parser_exec(&parser, tokenized, &root)) != CEGEX_OK) {
        printf("Error occured during parsing\n");
        goto free;
    }

    cegex_parse_node_print(root);

free:
    cegex_parse_node_free(root);
    cegex_tokenizer_free(&tokenizer);
    return status;
}

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
 *      |   OctalEscapeSequence;
 *
 * OctalEscapeSequence
 *  ::= "\" OctalDigit [ OctalDigit [ OctalDigit ] ];
 *
 * OctalDigit
 *  ::= /[0-7]/;
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

#define CEGEX_ASCII_CHARCODE_BEGIN           32
#define CEGEX_ASCII_CHARCODE_END             126

#define CEGEX_INTEGER_BUFFER_LENGTH          32
#define CEGEX_PARSER_CHARSET_CAPACITY        256

#define CEGEX_PARSING_PRECEDENCE_NONE        0
#define CEGEX_PARSING_PRECEDENCE_ALTERNATION 10
#define CEGEX_PARSING_PRECEDENCE_CONCAT      20
#define CEGEX_PARSING_PRECEDENCE_QUANTIFIER  30
#define CEGEX_PARSING_PRECEDENCE_LAZY        40

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

cegex_status cegex_character_set_add_range(cegex_character_set* set,
                                           cegex_uchar low,
                                           cegex_uchar high)
{
    cegex_status status = CEGEX_OK;
    cegex_uchar* set_characters = set->characters;
    for(cegex_uchar curr = low; curr <= high; curr += 1) {
        if(array_contains((void**) &set_characters, set->num_characters, sizeof(cegex_uchar), &curr, (comparator) cegex_uchar_eq))
            continue;

        CEGEX_TRY(
            cegex_character_set_add(set, curr));
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
        for(cegex_uchar curr = CEGEX_ASCII_CHARCODE_BEGIN; curr <= CEGEX_ASCII_CHARCODE_END; curr += 1) {
            if(array_contains((void**) &subset_characters, subset.num_characters, sizeof(cegex_uchar), &curr, (comparator) cegex_uchar_eq))
                continue;

            CEGEX_TRY(
                cegex_character_set_add(set, curr));
        }
    } else {
        for(size_t i = 0; i < subset.num_characters; i++) {
            CEGEX_TRY(
                cegex_character_set_add(set, subset.characters[i]));
        }
    }

fail:
    return status;
}

cegex_parse_node* cegex_parse_node_charset(
    cegex_character_set set)
{
    cegex_parse_node* node = malloc(sizeof(cegex_parse_node));
    node->type = CEGEX_PARSE_NODE_CHARSET;
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
    if(tok.type == CEGEX_TOKEN_SQUARE_CLOSE || tok.flags & CEGEX_TOKEN_FLG_PERL)
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

cegex_status cegex_parser_hex_digits(cegex_parser* ctx)
{
}

cegex_status cegex_parser_tok_charcode(cegex_parser* ctx,
                                       cegex_token tok,
                                       cegex_uchar* out)
{
    assert(ctx != NULL);
    assert(out != NULL);
    cegex_status status = CEGEX_OK;

    cegex_parsed_expr* working = cegex_parser_working_expr(ctx);
    if(tok.flags & CEGEX_TOKEN_FLG_ESC) {
        switch(tok.type) {
        case CEGEX_TOKEN_ESCAPE_HEX: {
            cegex_token d0 = { 0 }, d1 = { 0 };

            ITERATOR_CURRENT(&ctx->token_iterator, &d0);
            CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, CEGEX_TOKEN_FLG_HEX_DIGIT));

            ITERATOR_CURRENT(&ctx->token_iterator, &d1);
            CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, CEGEX_TOKEN_FLG_HEX_DIGIT));

            char buffer[3];
            buffer[0] = (char) working->tokenized.source[d0.start];
            buffer[1] = (char) working->tokenized.source[d1.start];
            if(!isxdigit(buffer[0]) || !isxdigit(buffer[1]))
                return CEGEX_ERR_SYNTAX;

            buffer[2] = '\0';

            char* end;
            size_t value = (size_t) strtoll(buffer, &end, 16);
            if(value > 0xFF)
                return CEGEX_ERR_UNEXPECTED;

            *out = (cegex_uchar) value;
        } break;

        case CEGEX_TOKEN_ESCAPE_OCTAL: {
            *out = 'o';
        } break;

        case CEGEX_TOKEN_ESCAPE_BELL: {
            *out = '\a';
        } break;
        case CEGEX_TOKEN_ESCAPE_CARRIAGE_RETURN: {
            *out = '\r';
        } break;
        case CEGEX_TOKEN_ESCAPE_FORM_FEED: {
            *out = '\f';
        } break;
        case CEGEX_TOKEN_ESCAPE_NEWLINE: {
            *out = '\n';
        } break;
        case CEGEX_TOKEN_ESCAPE_HORIZONTAL_TAB: {
            *out = '\t';
        } break;
        case CEGEX_TOKEN_ESCAPE_VERTICAL_TAB: {
            *out = '\v';
        } break;

        default: {
            assert(tok.length == 2);
            *out = working->tokenized.source[tok.start + 1];
        } break;
        }
    } else {
        assert(tok.length == 1);
        *out = working->tokenized.source[tok.start];
    }
fail:
    return status;
}

cegex_status cegex_parser_character_set(cegex_parser* ctx,
                                        cegex_parse_node** out)
{
    assert(ctx != NULL);
    assert(out != NULL);

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

                cegex_uchar low = 0, high = 0;
                CEGEX_TRY(cegex_parser_tok_charcode(ctx, previous, &low));
                CEGEX_TRY(cegex_parser_tok_charcode(ctx, peeked, &high));

                if(low > high) {
                    status = CEGEX_ERR_SYNTAX;
                    goto fail;
                }

                // Have to reset the previous token here, [a-b-c] would otherwise create two seperate ranges
                previous.type = CEGEX_TOKEN_UNKNOWN;
                CEGEX_TRY(cegex_character_set_add_range(&set, low, high));
                CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_MINUS));
                goto next;
            }
        }

        if(current.flags & CEGEX_TOKEN_FLG_ESC) {
            if(current.flags & CEGEX_TOKEN_FLG_PERL) {
                CEGEX_TRY(cegex_character_set_add_subset(&set, cegex_perl_set_by_tok_type(current.type)));
            } else {
                cegex_uchar charcode = 0;
                CEGEX_TRY(cegex_parser_tok_charcode(ctx, current, &charcode));
                CEGEX_TRY(cegex_character_set_add(&set, charcode));
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

    CEGEX_TRY(
        cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_SQUARE_CLOSE));

    *out = cegex_parse_node_charset(set);
    return status;

fail:
    return status;
}

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

    case CEGEX_TOKEN_CHARCODE:
        CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CHARCODE));
        *out = cegex_parse_node_charset(
            CEGEX_CHARSET_SINGLE(working->tokenized.source[current.start]));
        break;

    case CEGEX_TOKEN_CLASS_DOT: // .
        CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CLASS_DOT));
        *out = cegex_parse_node_charset(
            (cegex_character_set){
                .negated = true,
                .num_characters = 2,
                .characters = { '\n', '\r' } });
        break;

    case CEGEX_TOKEN_SQUARE_OPEN: { // [
        CEGEX_TRY(cegex_parser_character_set(ctx, out));
    } break;

    case CEGEX_TOKEN_CLASS_WORD:
    case CEGEX_TOKEN_CLASS_NOT_WORD: { // \w \W
        CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, CEGEX_TOKEN_FLG_PERL));
        bool negated = current.type == CEGEX_TOKEN_CLASS_NOT_WORD;

        *out = cegex_parse_node_charset(
            negated ? CEGEX_CHARSET_PERL_NOT_WORD : CEGEX_CHARSET_PERL_WORD);

    } break;

    case CEGEX_TOKEN_CLASS_DIGIT:
    case CEGEX_TOKEN_CLASS_NOT_DIGIT: { // \d \D
        CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, CEGEX_TOKEN_FLG_PERL));
        bool negated = current.type == CEGEX_TOKEN_CLASS_NOT_DIGIT;
        *out = cegex_parse_node_charset(
            negated ? CEGEX_CHARSET_PERL_NOT_DIGIT : CEGEX_CHARSET_PERL_DIGIT);

    } break;

    case CEGEX_TOKEN_CLASS_WHITESPACE:
    case CEGEX_TOKEN_CLASS_NOT_WHITESPACE: { // \s \S
        CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, CEGEX_TOKEN_FLG_PERL));
        bool negated = current.type == CEGEX_TOKEN_CLASS_NOT_WHITESPACE;

        *out = cegex_parse_node_charset(
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

        CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, CEGEX_TOKEN_FLG_SPECIAL));
        *out = cegex_parse_node_empty(node_type);
    } break;

    case CEGEX_TOKEN_PAREN_OPEN: {
        // group
    } break;

    default: {
        if(current.flags & CEGEX_TOKEN_FLG_ESC) {
            CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, CEGEX_TOKEN_FLG_ESC));

            cegex_uchar charcode;
            CEGEX_TRY(cegex_parser_tok_charcode(ctx, current, &charcode));
            *out = cegex_parse_node_charset(CEGEX_CHARSET_SINGLE(charcode));
        } else {
            CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, CEGEX_TOKEN_FLG_SPECIAL));
            *out = cegex_parse_node_charset(
                CEGEX_CHARSET_SINGLE(working->tokenized.source[current.start]));
        }
    } break;
    }

fail:
    return status;
}

cegex_status cegex_parser_integer(cegex_parser* ctx,
                                  size_t* out)
{
    assert(ctx != NULL);
    assert(out != NULL);

    cegex_status status = CEGEX_OK;

    size_t num_numbers = 0;
    cegex_uchar buffer[CEGEX_INTEGER_BUFFER_LENGTH];
    while(!ITERATOR_EMPTY(&ctx->token_iterator)) {
        if(num_numbers >= CEGEX_INTEGER_BUFFER_LENGTH - 1) {
            status = CEGEX_ERR_LIMIT;
            goto fail;
        }

        cegex_token current;
        ITERATOR_CURRENT(&ctx->token_iterator, &current);

        if(!(current.flags & CEGEX_TOKEN_FLG_NUMBER))
            break;

        CEGEX_TRY(
            cegex_parser_tok_charcode(ctx, current, &buffer[num_numbers++]));

        ITERATOR_NEXT(&ctx->token_iterator);
    }

    if(num_numbers == 0)
        return CEGEX_ERR_SYNTAX;

    buffer[num_numbers] = '\0';

    char* end;
    *out = ((size_t) strtoll((const char*) buffer, &end, 10));

fail:
    return status;
}

void cegex_parser_skip_whitespace(cegex_parser* ctx)
{
    assert(ctx != NULL);

    cegex_token current;
    while(!ITERATOR_EMPTY(&ctx->token_iterator)) {
        ITERATOR_CURRENT(&ctx->token_iterator, &current);
        if(current.flags & CEGEX_TOKEN_FLG_WHITESPACE) {
            ITERATOR_NEXT(&ctx->token_iterator);
            continue;
        } else {
            break;
        }
    }
}

cegex_status cegex_parser_counted_quantifier(cegex_parser* ctx,
                                             cegex_parse_node** out)
{
    assert(ctx != NULL);
    assert(out != NULL);
    assert(*out != NULL);

    size_t min = 0, max = 0;
    cegex_status status = CEGEX_OK;
    cegex_counted_quantifier_type type;

    CEGEX_TRY(
        cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CURLY_OPEN));

    cegex_parser_skip_whitespace(ctx);
    if(CEGEX_OK != cegex_parser_integer(ctx, &min)) {
        status = CEGEX_ERR_UNEXPECTED;
        goto fail;
    }

    cegex_parser_skip_whitespace(ctx);
    if(CEGEX_OK == cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CURLY_CLOSE)) {
        type = CEGEX_COUNTED_QUANTIFIER_EXACT;
    } else {
        cegex_parser_skip_whitespace(ctx);
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

        if(current.flags & CEGEX_TOKEN_FLG_NUMBER) {
            if(CEGEX_OK != cegex_parser_integer(ctx, &max)) {
                status = CEGEX_ERR_UNEXPECTED;
                goto fail;
            }

            cegex_parser_skip_whitespace(ctx);
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
                // this "error" is recoverable. However, any other error is a reason to abort the parsing process.
                goto fail;
            }
        }

        if(current.type == CEGEX_TOKEN_ALTERNATE) {
            if(precedence > CEGEX_PARSING_PRECEDENCE_ALTERNATION)
                break;

            CEGEX_TRY(
                cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_ALTERNATE));

            cegex_parse_node* rhs = NULL;
            CEGEX_TRY(
                cegex_parser_expr(ctx, CEGEX_PARSING_PRECEDENCE_ALTERNATION + 1, &rhs));

            lhs = cegex_parse_node_binary(CEGEX_PARSE_NODE_ALTERNATE, lhs, rhs);
        } else if(cegex_token_starts_primary(current)) {
            // Implicit concatination without an infix operator
            if(precedence > CEGEX_PARSING_PRECEDENCE_CONCAT)
                break;

            cegex_parse_node* rhs = NULL;
            CEGEX_TRY(
                cegex_parser_expr(ctx, CEGEX_PARSING_PRECEDENCE_CONCAT + 1, &rhs));

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

    CEGEX_TRY(cegex_parser_expr(ctx, CEGEX_PARSING_PRECEDENCE_NONE, &ctx->parsed[ctx->num_parsed].root));
    if(out != NULL) {
        *out = ctx->parsed[ctx->num_parsed].root;
    }

    ctx->num_parsed += 1;

fail:
    return status;
}

/***************************************** */
typedef struct cegex_ascii_table_entry
{
    cegex_uchar code;
    const char* repr;
} cegex_ascii_table_entry;

static cegex_ascii_table_entry ascii_table[] = {
    /* Control characters (0–31) */
    { .code = 0, .repr = "<NUL>" },
    { .code = 1, .repr = "<SOH>" },
    { .code = 2, .repr = "<STX>" },
    { .code = 3, .repr = "<ETX>" },
    { .code = 4, .repr = "<EOT>" },
    { .code = 5, .repr = "<ENQ>" },
    { .code = 6, .repr = "<ACK>" },
    { .code = 7, .repr = "<BEL>" },
    { .code = 8, .repr = "<BS>" },
    { .code = 9, .repr = "<HT>" },
    { .code = 10, .repr = "<LF>" },
    { .code = 11, .repr = "<VT>" },
    { .code = 12, .repr = "<FF>" },
    { .code = 13, .repr = "<CR>" },
    { .code = 14, .repr = "<SO>" },
    { .code = 15, .repr = "<SI>" },
    { .code = 16, .repr = "<DLE>" },
    { .code = 17, .repr = "<DC1>" },
    { .code = 18, .repr = "<DC2>" },
    { .code = 19, .repr = "<DC3>" },
    { .code = 20, .repr = "<DC4>" },
    { .code = 21, .repr = "<NAK>" },
    { .code = 22, .repr = "<SYN>" },
    { .code = 23, .repr = "<ETB>" },
    { .code = 24, .repr = "<CAN>" },
    { .code = 25, .repr = "<EM>" },
    { .code = 26, .repr = "<SUB>" },
    { .code = 27, .repr = "<ESC>" },
    { .code = 28, .repr = "<FS>" },
    { .code = 29, .repr = "<GS>" },
    { .code = 30, .repr = "<RS>" },
    { .code = 31, .repr = "<US>" },

    { .code = 32, .repr = "<SP>" },
    { .code = 33, .repr = "!" },
    { .code = 34, .repr = "\"" },
    { .code = 35, .repr = "#" },
    { .code = 36, .repr = "$" },
    { .code = 37, .repr = "%" },
    { .code = 38, .repr = "&" },
    { .code = 39, .repr = "'" },
    { .code = 40, .repr = "(" },
    { .code = 41, .repr = ")" },
    { .code = 42, .repr = "*" },
    { .code = 43, .repr = "+" },
    { .code = 44, .repr = "," },
    { .code = 45, .repr = "-" },
    { .code = 46, .repr = "." },
    { .code = 47, .repr = "/" },

    { .code = 48, .repr = "0" },
    { .code = 49, .repr = "1" },
    { .code = 50, .repr = "2" },
    { .code = 51, .repr = "3" },
    { .code = 52, .repr = "4" },
    { .code = 53, .repr = "5" },
    { .code = 54, .repr = "6" },
    { .code = 55, .repr = "7" },
    { .code = 56, .repr = "8" },
    { .code = 57, .repr = "9" },

    { .code = 58, .repr = ":" },
    { .code = 59, .repr = ";" },
    { .code = 60, .repr = "<" },
    { .code = 61, .repr = "=" },
    { .code = 62, .repr = ">" },
    { .code = 63, .repr = "?" },
    { .code = 64, .repr = "@" },

    { .code = 65, .repr = "A" },
    { .code = 66, .repr = "B" },
    { .code = 67, .repr = "C" },
    { .code = 68, .repr = "D" },
    { .code = 69, .repr = "E" },
    { .code = 70, .repr = "F" },
    { .code = 71, .repr = "G" },
    { .code = 72, .repr = "H" },
    { .code = 73, .repr = "I" },
    { .code = 74, .repr = "J" },
    { .code = 75, .repr = "K" },
    { .code = 76, .repr = "L" },
    { .code = 77, .repr = "M" },
    { .code = 78, .repr = "N" },
    { .code = 79, .repr = "O" },
    { .code = 80, .repr = "P" },
    { .code = 81, .repr = "Q" },
    { .code = 82, .repr = "R" },
    { .code = 83, .repr = "S" },
    { .code = 84, .repr = "T" },
    { .code = 85, .repr = "U" },
    { .code = 86, .repr = "V" },
    { .code = 87, .repr = "W" },
    { .code = 88, .repr = "X" },
    { .code = 89, .repr = "Y" },
    { .code = 90, .repr = "Z" },

    { .code = 91, .repr = "[" },
    { .code = 92, .repr = "\\" },
    { .code = 93, .repr = "]" },
    { .code = 94, .repr = "^" },
    { .code = 95, .repr = "_" },
    { .code = 96, .repr = "`" },

    { .code = 97, .repr = "a" },
    { .code = 98, .repr = "b" },
    { .code = 99, .repr = "c" },
    { .code = 100, .repr = "d" },
    { .code = 101, .repr = "e" },
    { .code = 102, .repr = "f" },
    { .code = 103, .repr = "g" },
    { .code = 104, .repr = "h" },
    { .code = 105, .repr = "i" },
    { .code = 106, .repr = "j" },
    { .code = 107, .repr = "k" },
    { .code = 108, .repr = "l" },
    { .code = 109, .repr = "m" },
    { .code = 110, .repr = "n" },
    { .code = 111, .repr = "o" },
    { .code = 112, .repr = "p" },
    { .code = 113, .repr = "q" },
    { .code = 114, .repr = "r" },
    { .code = 115, .repr = "s" },
    { .code = 116, .repr = "t" },
    { .code = 117, .repr = "u" },
    { .code = 118, .repr = "v" },
    { .code = 119, .repr = "w" },
    { .code = 120, .repr = "x" },
    { .code = 121, .repr = "y" },
    { .code = 122, .repr = "z" },

    { .code = 123, .repr = "{" },
    { .code = 124, .repr = "|" },
    { .code = 125, .repr = "}" },
    { .code = 126, .repr = "~" },

    { .code = 127, .repr = "<DEL>" }
};

static const char* cegex_ascii_table_get_repr(cegex_uchar code)
{
    const size_t length = sizeof(ascii_table) / sizeof(cegex_ascii_table_entry);
    for(size_t i = 0; i < length; i++) {
        if(ascii_table[i].code == code)
            return ascii_table[i].repr;
    }

    return "UNK";
}

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
        printf("%s", cegex_ascii_table_get_repr(current));

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

    const char* expr = "[A-Za-z][A-Za-z0-9:_-]*";
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

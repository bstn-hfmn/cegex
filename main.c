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

#define CEGEX_DIGIT_BUFFER_LENGTH            32
#define CEGEX_MAX_GROUPS                     256
#define CEGEX_PARSER_CHARSET_CAPACITY        256

#define CEGEX_PARSING_PRECEDENCE_NONE        0
#define CEGEX_PARSING_PRECEDENCE_ALTERNATION 10
#define CEGEX_PARSING_PRECEDENCE_CONCAT      20
#define CEGEX_PARSING_PRECEDENCE_QUANTIFIER  30

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
            uint32_t index;
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
    size_t num_groups;

    cegex_parse_node* root;
    cegex_tokenized_expr tokenized;
} cegex_parsed_expr;

typedef ARRAY(uint32_t) cegex_group_id_stack;
typedef struct cegex_parser
{
    cegex_token_iterator token_iterator;
    cegex_group_id_stack group_id_stack;

    size_t num_parsed;
    cegex_parsed_expr parsed[CEGEX_EXPRESSION_CAPACITY];
} cegex_parser;

typedef cegex_status (*cegex_parsing_fn)(
    cegex_parser* ctx,
    cegex_token tok,
    uint32_t precedence,
    cegex_parse_node* lhs,
    cegex_parse_node** out);

typedef struct cegex_parsing_rule
{
    uint32_t precedence;
    cegex_token_type type;
    cegex_parsing_fn parse;
} cegex_parsing_rule;

#define CEGEX_DECL_PARSING_FN(name)          \
    cegex_status name(cegex_parser* ctx,     \
                      cegex_token tok,       \
                      uint32_t precedence,   \
                      cegex_parse_node* lhs, \
                      cegex_parse_node** out)

CEGEX_DECL_PARSING_FN(cegex_parser_charcode);
CEGEX_DECL_PARSING_FN(cegex_parser_perl);
CEGEX_DECL_PARSING_FN(cegex_parser_quantifier);
CEGEX_DECL_PARSING_FN(cegex_parser_alternate);
CEGEX_DECL_PARSING_FN(cegex_parser_character_set);
CEGEX_DECL_PARSING_FN(cegex_parser_group);
CEGEX_DECL_PARSING_FN(cegex_parser_anchor);

static const cegex_parsing_rule prefix_rules[] = {
    { CEGEX_PARSING_PRECEDENCE_NONE, CEGEX_TOKEN_CHARCODE, cegex_parser_charcode },

    { CEGEX_PARSING_PRECEDENCE_NONE, CEGEX_TOKEN_CLASS_DOT, cegex_parser_perl },
    { CEGEX_PARSING_PRECEDENCE_NONE, CEGEX_TOKEN_CLASS_WORD, cegex_parser_perl },
    { CEGEX_PARSING_PRECEDENCE_NONE, CEGEX_TOKEN_CLASS_NOT_WORD, cegex_parser_perl },
    { CEGEX_PARSING_PRECEDENCE_NONE, CEGEX_TOKEN_CLASS_DIGIT, cegex_parser_perl },
    { CEGEX_PARSING_PRECEDENCE_NONE, CEGEX_TOKEN_CLASS_NOT_DIGIT, cegex_parser_perl },
    { CEGEX_PARSING_PRECEDENCE_NONE, CEGEX_TOKEN_CLASS_WHITESPACE, cegex_parser_perl },
    { CEGEX_PARSING_PRECEDENCE_NONE, CEGEX_TOKEN_CLASS_NOT_WHITESPACE, cegex_parser_perl },

    { CEGEX_PARSING_PRECEDENCE_NONE, CEGEX_TOKEN_SQUARE_OPEN, cegex_parser_character_set },
    { CEGEX_PARSING_PRECEDENCE_NONE, CEGEX_TOKEN_PAREN_OPEN, cegex_parser_group },

    { CEGEX_PARSING_PRECEDENCE_NONE, CEGEX_TOKEN_HAT, cegex_parser_anchor },
    { CEGEX_PARSING_PRECEDENCE_NONE, CEGEX_TOKEN_DOLLAR, cegex_parser_anchor },
    { CEGEX_PARSING_PRECEDENCE_NONE, CEGEX_TOKEN_ANCHOR_BOUNDARY, cegex_parser_anchor },
    { CEGEX_PARSING_PRECEDENCE_NONE, CEGEX_TOKEN_ANCHOR_NOT_BOUNDARY, cegex_parser_anchor },
};

static const cegex_parsing_rule infix_rules[] = {
    { CEGEX_PARSING_PRECEDENCE_ALTERNATION, CEGEX_TOKEN_ALTERNATE, cegex_parser_alternate }
};

static const cegex_parsing_rule suffix_rules[] = {
    { CEGEX_PARSING_PRECEDENCE_QUANTIFIER, CEGEX_TOKEN_STAR, cegex_parser_quantifier },
    { CEGEX_PARSING_PRECEDENCE_QUANTIFIER, CEGEX_TOKEN_PLUS, cegex_parser_quantifier },
    { CEGEX_PARSING_PRECEDENCE_QUANTIFIER, CEGEX_TOKEN_QUESTION_MARK, cegex_parser_quantifier },
    { CEGEX_PARSING_PRECEDENCE_QUANTIFIER, CEGEX_TOKEN_CURLY_OPEN, cegex_parser_quantifier },
};

bool cegex_token_starts_primary(cegex_token tok)
{
    if(tok.type == CEGEX_TOKEN_PLUS ||
       tok.type == CEGEX_TOKEN_QUESTION_MARK ||
       tok.type == CEGEX_TOKEN_STAR ||
       tok.type == CEGEX_TOKEN_PAREN_CLOSE)
        return false;

    return true;
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
    cegex_status status = CEGEX_ERR_UNKNOWN;
    while(low <= high) {
        CEGEX_TRY(
            cegex_character_set_add(set, low));

        if(low == 0xFF)
            break;

        ++low;
    };

    return CEGEX_OK;
fail:
    return status;
}

cegex_status cegex_character_set_add_subset(cegex_character_set* set,
                                            cegex_character_set subset)
{
    cegex_status status = CEGEX_ERR_UNKNOWN;

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

    return CEGEX_OK;
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
    cegex_status status = CEGEX_ERR_UNKNOWN;

    cegex_token tok;
    if(CEGEX_OK != ITERATOR_CURRENT(&ctx->token_iterator, &tok)) {
        status = CEGEX_ERR_UNEXPECTED;
        goto fail;
    }

    if(type != tok.type) {
        status = CEGEX_ERR_UNEXPECTED;
        goto fail;
    }

    ITERATOR_NEXT(&ctx->token_iterator);
    return CEGEX_OK;

fail:
    return status;
}

cegex_status cegex_parser_expect_tok_flg(cegex_parser* ctx,
                                         cegex_token_flags flags)
{
    assert(ctx != NULL);
    cegex_status status = CEGEX_ERR_UNKNOWN;

    cegex_token tok;
    if(CEGEX_OK != ITERATOR_CURRENT(&ctx->token_iterator, &tok)) {
        status = CEGEX_ERR_UNEXPECTED;
        goto fail;
    }

    if(!((tok.flags & flags) > 0)) {
        status = CEGEX_ERR_UNEXPECTED;
        goto fail;
    }

    ITERATOR_NEXT(&ctx->token_iterator);
    return CEGEX_OK;

fail:
    return status;
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

cegex_status cegex_parser_expect_charcode(cegex_parser* ctx,
                                          cegex_uchar* out);

cegex_status cegex_parser_unsigned_digit(cegex_parser* ctx,
                                         size_t limit,
                                         int radix,
                                         uint64_t* out)
{
    assert(ctx != NULL);
    assert(out != NULL);
    assert(radix == 10 || radix == 16 || radix == 8);

    cegex_status status = CEGEX_ERR_UNKNOWN;

    cegex_token_flags flags = CEGEX_TOKEN_FLG_DIGIT;
    flags = radix == 8 ? CEGEX_TOKEN_FLG_OCT_DIGIT : flags;
    flags = radix == 16 ? CEGEX_TOKEN_FLG_HEX_DIGIT : flags;

    size_t num_digits = 0;
    char buffer[CEGEX_DIGIT_BUFFER_LENGTH];

    while(!ITERATOR_EMPTY(&ctx->token_iterator)) {
        if(num_digits >= CEGEX_DIGIT_BUFFER_LENGTH - 1) {
            status = CEGEX_ERR_LIMIT;
            goto fail;
        }

        if(limit > 0 && num_digits == limit)
            break;

        cegex_token current;
        ITERATOR_CURRENT(&ctx->token_iterator, &current);

        if(!(current.flags & flags))
            break;

        cegex_uchar digit_charcode;
        CEGEX_TRY(
            cegex_parser_expect_charcode(ctx, &digit_charcode));

        buffer[num_digits++] = (char) digit_charcode;
    }

    if(num_digits == 0 || (limit > 0 && num_digits != limit))
        return CEGEX_ERR_UNEXPECTED;

    buffer[num_digits] = '\0';

    char* end;
    *out = ((uint64_t) strtoll((const char*) buffer, &end, radix));

    return CEGEX_OK;

fail:
    return status;
}

cegex_status cegex_parser_expect_charcode(cegex_parser* ctx,
                                          cegex_uchar* out)
{
    assert(ctx != NULL);
    assert(out != NULL);
    cegex_status status = CEGEX_ERR_UNKNOWN;

    cegex_token tok;
    CEGEX_TRY(ITERATOR_CURRENT(&ctx->token_iterator, &tok));

    const cegex_parsed_expr* working = cegex_parser_working_expr(ctx);
    if(tok.flags & CEGEX_TOKEN_FLG_ESC) {
        switch(tok.type) {
        case CEGEX_TOKEN_ESCAPE_HEX: {
            CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_ESCAPE_HEX));

            uint64_t value = 0;
            CEGEX_TRY(cegex_parser_unsigned_digit(ctx, 2, 16, &value));

            if(value > 0xFF)
                return CEGEX_ERR_UNEXPECTED;

            *out = (cegex_uchar) value;
        } break;

        case CEGEX_TOKEN_ESCAPE_OCTAL: {
            CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_ESCAPE_OCTAL));

            uint64_t value = 0;
            size_t rewind_index = ctx->token_iterator.index;

            for(size_t num_digits = 1; num_digits <= 3; num_digits++) {
                ctx->token_iterator.index = rewind_index;
                if(CEGEX_OK != (status = cegex_parser_unsigned_digit(ctx, num_digits, 8, &value)))
                    break;
            }

            if(value > 0xFF)
                return CEGEX_ERR_UNEXPECTED;

            *out = (cegex_uchar) value;
        } break;

        case CEGEX_TOKEN_ESCAPE_BELL: {
            CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_ESCAPE_BELL));
            *out = '\a';
        } break;
        case CEGEX_TOKEN_ESCAPE_CARRIAGE_RETURN: {
            CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_ESCAPE_CARRIAGE_RETURN));
            *out = '\r';
        } break;
        case CEGEX_TOKEN_ESCAPE_FORM_FEED: {
            CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_ESCAPE_FORM_FEED));
            *out = '\f';
        } break;
        case CEGEX_TOKEN_ESCAPE_NEWLINE: {
            CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_ESCAPE_NEWLINE));
            *out = '\n';
        } break;
        case CEGEX_TOKEN_ESCAPE_HORIZONTAL_TAB: {
            CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_ESCAPE_HORIZONTAL_TAB));
            *out = '\t';
        } break;
        case CEGEX_TOKEN_ESCAPE_VERTICAL_TAB: {
            CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_ESCAPE_VERTICAL_TAB));
            *out = '\v';
        } break;

        default: {
            CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, CEGEX_TOKEN_FLG_ESC));

            assert(tok.length == 2);
            *out = working->tokenized.source[tok.start + 1];
        } break;
        }
    } else {
        ITERATOR_NEXT(&ctx->token_iterator);

        assert(tok.length == 1);
        *out = working->tokenized.source[tok.start];
    }

    return CEGEX_OK;

fail:
    return status;
}

cegex_status cegex_parser_expr(cegex_parser* ctx,
                               uint32_t precedence,
                               cegex_parse_node** out);

cegex_status cegex_parser_charcode(cegex_parser* ctx,
                                   cegex_token tok,
                                   uint32_t precedence,
                                   cegex_parse_node* lhs,
                                   cegex_parse_node** out)
{
    (void) tok;
    (void) lhs;
    (void) precedence;

    assert(ctx != NULL);
    assert(out != NULL);
    cegex_status status = CEGEX_ERR_UNKNOWN;

    cegex_uchar charcode;
    CEGEX_TRY(cegex_parser_expect_charcode(ctx, &charcode));
    *out = cegex_parse_node_charset(CEGEX_CHARSET_SINGLE(charcode));

    return CEGEX_OK;

fail:
    return status;
}

cegex_status cegex_parser_perl(cegex_parser* ctx,
                               cegex_token tok,
                               uint32_t precedence,
                               cegex_parse_node* lhs,
                               cegex_parse_node** out)
{
    (void) lhs;
    (void) tok;
    (void) precedence;

    assert(ctx != NULL);
    assert(out != NULL);

    cegex_status status = CEGEX_ERR_UNKNOWN;

    cegex_token current;
    CEGEX_TRY(ITERATOR_CURRENT(&ctx->token_iterator, &current));

    switch(current.type) {
    case CEGEX_TOKEN_CLASS_DOT:
        // Any '.' = [^\n\r]
        CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CLASS_DOT));
        *out = cegex_parse_node_charset(
            (cegex_character_set){
                .negated = true,
                .num_characters = 2,
                .characters = { '\n', '\r' } });
        break;

    case CEGEX_TOKEN_CLASS_WORD:
    case CEGEX_TOKEN_CLASS_NOT_WORD: {
        // \w \W
        CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, CEGEX_TOKEN_FLG_PERL));
        bool negated = current.type == CEGEX_TOKEN_CLASS_NOT_WORD;

        *out = cegex_parse_node_charset(
            negated ? CEGEX_CHARSET_PERL_NOT_WORD : CEGEX_CHARSET_PERL_WORD);

    } break;

    case CEGEX_TOKEN_CLASS_DIGIT:
    case CEGEX_TOKEN_CLASS_NOT_DIGIT: {
        // \d \D
        CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, CEGEX_TOKEN_FLG_PERL));
        bool negated = current.type == CEGEX_TOKEN_CLASS_NOT_DIGIT;
        *out = cegex_parse_node_charset(
            negated ? CEGEX_CHARSET_PERL_NOT_DIGIT : CEGEX_CHARSET_PERL_DIGIT);

    } break;

    case CEGEX_TOKEN_CLASS_WHITESPACE:
    case CEGEX_TOKEN_CLASS_NOT_WHITESPACE: {
        // \s \S
        CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, CEGEX_TOKEN_FLG_PERL));
        bool negated = current.type == CEGEX_TOKEN_CLASS_NOT_WHITESPACE;

        *out = cegex_parse_node_charset(
            negated ? CEGEX_CHARSET_PERL_NOT_SPACE : CEGEX_CHARSET_PERL_SPACE);

    } break;

    default: {
        status = CEGEX_ERR_UNEXPECTED;
        goto fail;
    };
    }

    return CEGEX_OK;
fail:
    return status;
}

cegex_status cegex_parser_group(cegex_parser* ctx,
                                cegex_token tok,
                                uint32_t precedence,
                                cegex_parse_node* lhs,
                                cegex_parse_node** out)
{
    (void) tok;
    (void) lhs;
    (void) precedence;

    cegex_status status = CEGEX_ERR_UNKNOWN;
    cegex_parsed_expr* working = cegex_parser_working_expr(ctx);

    CEGEX_TRY(
        cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_PAREN_OPEN));

    bool is_capturing = true;
    if(CEGEX_OK == cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_QUESTION_MARK)) {
        CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_COLON));
        is_capturing = false;
    }

    if(ctx->group_id_stack.length >= CEGEX_MAX_GROUPS) {
        status = CEGEX_ERR_LIMIT;
        goto fail;
    }

    CEGEX_TRY(
        ARRAY_PUSH(&ctx->group_id_stack, working->num_groups));

    if(is_capturing)
        working->num_groups++;

    cegex_parse_node* expr = NULL;
    status = cegex_parser_expr(ctx, CEGEX_PARSING_PRECEDENCE_NONE, &expr);

    uint32_t group_id;
    ARRAY_POP(&ctx->group_id_stack, &group_id);

    if(status != CEGEX_OK)
        goto fail;

    if(CEGEX_OK != (status = cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_PAREN_CLOSE))) {
        cegex_parse_node_free(expr);
        goto fail;
    }

    cegex_parse_node* node = cegex_parse_node_unary(CEGEX_PARSE_NODE_GROUP, expr);
    node->data.group.is_capturing = is_capturing;
    node->data.group.index = is_capturing ? group_id : 0;

    *out = node;
    return CEGEX_OK;
fail:
    return status;
}

cegex_status cegex_parser_character_set(cegex_parser* ctx,
                                        cegex_token tok,
                                        uint32_t precedence,
                                        cegex_parse_node* lhs,
                                        cegex_parse_node** out)
{
    (void) tok;
    (void) precedence;
    (void) lhs;

    assert(ctx != NULL);
    assert(out != NULL);

    cegex_status status = CEGEX_ERR_UNKNOWN;

    cegex_character_set set = { 0 };
    CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_SQUARE_OPEN));

    cegex_uchar previous_charcode = 0;
    cegex_token current = { 0 }, previous = { 0 };
    ITERATOR_CURRENT(&ctx->token_iterator, &current);
    if(current.type == CEGEX_TOKEN_HAT) {
        set.negated = true;
        CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_HAT));
    }

    ITERATOR_CURRENT(&ctx->token_iterator, &current);
    while(!ITERATOR_EMPTY(&ctx->token_iterator) && current.type != CEGEX_TOKEN_SQUARE_CLOSE) {
        cegex_token peeked;
        if(CEGEX_OK == ITERATOR_PEEK(&ctx->token_iterator, &peeked)) {
            if(current.type == CEGEX_TOKEN_MINUS &&
               previous.type != CEGEX_TOKEN_UNKNOWN &&
               cegex_token_is_charset_range_rune(previous) &&
               cegex_token_is_charset_range_rune(peeked)) {

                cegex_uchar low = previous_charcode, high = 0;
                CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_MINUS));
                CEGEX_TRY(cegex_parser_expect_charcode(ctx, &high));

                if(low > high) {
                    status = CEGEX_ERR_SYNTAX;
                    goto fail;
                }

                // Have to reset the previous token here, [a-b-c] would otherwise create two seperate ranges
                previous.type = CEGEX_TOKEN_UNKNOWN;
                CEGEX_TRY(cegex_character_set_add_range(&set, low, high));
                goto next;
            }
        }

        cegex_uchar charcode;
        CEGEX_TRY(cegex_parser_expect_charcode(ctx, &charcode));

        if(current.flags & CEGEX_TOKEN_FLG_ESC) {
            if(current.flags & CEGEX_TOKEN_FLG_PERL) {
                CEGEX_TRY(cegex_character_set_add_subset(&set, cegex_perl_set_by_tok_type(current.type)));
            } else {
                CEGEX_TRY(cegex_character_set_add(&set, charcode));
            }
        } else {
            assert(current.length == 1);
            CEGEX_TRY(cegex_character_set_add(&set, charcode));
        }

        previous = current;
        previous_charcode = charcode;
    next:
        ITERATOR_CURRENT(&ctx->token_iterator, &current);
    }

    if(set.num_characters == 0) {
        status = CEGEX_ERR_SYNTAX;
        goto fail;
    }

    CEGEX_TRY(
        cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_SQUARE_CLOSE));

    *out = cegex_parse_node_charset(set);
    return CEGEX_OK;

fail:
    return status;
}

cegex_status cegex_parser_anchor(cegex_parser* ctx,
                                 cegex_token tok,
                                 uint32_t precedence,
                                 cegex_parse_node* lhs,
                                 cegex_parse_node** out)
{
    (void) lhs;
    (void) tok;
    (void) precedence;

    assert(ctx != NULL);
    assert(out != NULL);

    cegex_status status = CEGEX_ERR_UNKNOWN;

    cegex_token current;
    CEGEX_TRY(ITERATOR_CURRENT(&ctx->token_iterator, &current));

    if(current.type != CEGEX_TOKEN_DOLLAR &&
       current.type != CEGEX_TOKEN_HAT &&
       current.type != CEGEX_TOKEN_ANCHOR_BOUNDARY &&
       current.type != CEGEX_TOKEN_ANCHOR_NOT_BOUNDARY) {
        status = CEGEX_ERR_UNEXPECTED;
        goto fail;
    }

    cegex_parse_node_type node_type = CEGEX_PARSE_NODE_ANCHOR_START;
    node_type = (current.type == CEGEX_TOKEN_DOLLAR) ? CEGEX_PARSE_NODE_ANCHOR_END : node_type;
    node_type = (current.type == CEGEX_TOKEN_ANCHOR_BOUNDARY) ? CEGEX_PARSE_NODE_ANCHOR_WORD_BOUNDARY : node_type;
    node_type = (current.type == CEGEX_TOKEN_ANCHOR_NOT_BOUNDARY) ? CEGEX_PARSE_NODE_ANCHOR_NOT_WORD_BOUNDARY : node_type;

    CEGEX_TRY(
        cegex_parser_expect_tok_typ(ctx, current.type));

    *out = cegex_parse_node_empty(node_type);
    return CEGEX_OK;
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

cegex_status cegex_parser_counted_quantifier(cegex_parser* ctx,
                                             cegex_parse_node* lhs,
                                             cegex_parse_node** out)
{
    assert(ctx != NULL);
    assert(out != NULL);
    assert(lhs != NULL);

    size_t min = 0, max = 0;
    cegex_status status = CEGEX_ERR_UNKNOWN;
    cegex_counted_quantifier_type type;

    CEGEX_TRY(
        cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CURLY_OPEN));

    cegex_parser_skip_whitespace(ctx);
    CEGEX_TRY(cegex_parser_unsigned_digit(ctx, 0, 10, &min));

    cegex_parser_skip_whitespace(ctx);
    if(CEGEX_OK == cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CURLY_CLOSE)) {
        type = CEGEX_COUNTED_QUANTIFIER_EXACT;
    } else {
        cegex_parser_skip_whitespace(ctx);
        CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_COMMA));

        cegex_parser_skip_whitespace(ctx);
        cegex_token current;
        if(CEGEX_OK != ITERATOR_CURRENT(&ctx->token_iterator, &current)) {
            status = CEGEX_ERR_UNEXPECTED;
            goto fail;
        }

        if(current.flags & CEGEX_TOKEN_FLG_DIGIT) {
            CEGEX_TRY(cegex_parser_unsigned_digit(ctx, 0, 10, &max));

            cegex_parser_skip_whitespace(ctx);
            CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CURLY_CLOSE));

            if(min > max) {
                status = CEGEX_ERR_SYNTAX;
                goto fail;
            }

            type = CEGEX_COUNTED_QUANTIFIER_MIN_MAX;
        } else {
            cegex_parser_skip_whitespace(ctx);
            CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CURLY_CLOSE));

            type = CEGEX_COUNTED_QUANTIFIER_MIN;
        }
    }

    cegex_parse_node* node = cegex_parse_node_unary(CEGEX_PARSE_NODE_QUANTIFIER_COUNTED, lhs);
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

cegex_status cegex_parser_quantifier(cegex_parser* ctx,
                                     cegex_token tok,
                                     uint32_t precedence,
                                     cegex_parse_node* lhs,
                                     cegex_parse_node** out)
{
    (void) tok;
    (void) precedence;

    assert(ctx != NULL);
    assert(lhs != NULL);
    assert(out != NULL);

    if(cegex_parse_node_is_quantifier(lhs))
        return CEGEX_ERR_SYNTAX;

    cegex_parse_node* quantified = NULL;

    cegex_status status = CEGEX_ERR_UNKNOWN;
    cegex_token current;
    CEGEX_TRY(
        ITERATOR_CURRENT(&ctx->token_iterator, &current));

    if(current.type == CEGEX_TOKEN_CURLY_OPEN) {
        const size_t rewind_index = ctx->token_iterator.index;

        status = cegex_parser_counted_quantifier(ctx, lhs, &quantified);
        if(CEGEX_ERR_UNEXPECTED == status) {
            *out = lhs;
            ctx->token_iterator.index = rewind_index;
            goto fail;
        } else if(CEGEX_OK != status) {
            goto fail;
        }
    } else {
        cegex_parse_node_type node_type;
        switch(current.type) {
        case CEGEX_TOKEN_STAR:
            node_type = CEGEX_PARSE_NODE_QUANTIFIER_STAR;
            CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_STAR));
            break;

        case CEGEX_TOKEN_PLUS:
            node_type = CEGEX_PARSE_NODE_QUANTIFIER_PLUS;
            CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_PLUS));
            break;

        case CEGEX_TOKEN_QUESTION_MARK:
            node_type = CEGEX_PARSE_NODE_QUANTIFIER_OPTIONAL;
            CEGEX_TRY(cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_QUESTION_MARK));
            break;

        default:
            status = CEGEX_ERR_SYNTAX;
            goto fail;
        }

        quantified = cegex_parse_node_unary(node_type, lhs);
    }

    if(CEGEX_OK == cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_QUESTION_MARK)) {
        quantified = cegex_parse_node_unary(CEGEX_PARSE_NODE_QUANTIFIER_LAZY, quantified);
    }

    *out = quantified;
    return CEGEX_OK;

fail:
    cegex_parse_node_free(quantified);
    return status;
}

cegex_status cegex_parser_alternate(cegex_parser* ctx,
                                    cegex_token tok,
                                    uint32_t precedence,
                                    cegex_parse_node* lhs,
                                    cegex_parse_node** out)
{
    (void) tok;

    cegex_status status = CEGEX_ERR_UNKNOWN;
    cegex_parse_node* rhs = NULL;

    CEGEX_TRY(
        cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_ALTERNATE));

    CEGEX_TRY(
        cegex_parser_expr(ctx, precedence, &rhs));

    *out = cegex_parse_node_binary(CEGEX_PARSE_NODE_ALTERNATE, lhs, rhs);
    return CEGEX_OK;

fail:
    cegex_parse_node_free(rhs);
    return status;
}

const cegex_parsing_rule* cegex_parser_find_prefix(cegex_token_type type)
{
    const size_t length = sizeof(prefix_rules) / sizeof(cegex_parsing_rule);
    for(size_t i = 0; i < length; i++) {
        if(prefix_rules[i].type == type) {
            return &prefix_rules[i];
        }
    }

    return NULL;
}

const cegex_parsing_rule* cegex_parser_find_suffix(cegex_token_type type)
{
    const size_t length = sizeof(suffix_rules) / sizeof(cegex_parsing_rule);
    for(size_t i = 0; i < length; i++) {
        if(suffix_rules[i].type == type) {
            return &suffix_rules[i];
        }
    }

    return NULL;
}

const cegex_parsing_rule* cegex_parser_find_infix(cegex_token_type type)
{
    const size_t length = sizeof(infix_rules) / sizeof(cegex_parsing_rule);
    for(size_t i = 0; i < length; i++) {
        if(infix_rules[i].type == type) {
            return &infix_rules[i];
        }
    }

    return NULL;
}

cegex_status cegex_parser_expr(cegex_parser* ctx,
                               uint32_t precedence,
                               cegex_parse_node** out)
{
    cegex_status status = CEGEX_ERR_UNKNOWN;

    cegex_token current;
    cegex_parse_node* lhs = NULL;

    CEGEX_TRY(ITERATOR_CURRENT(&ctx->token_iterator, &current));

    const cegex_parsing_rule* prefix = cegex_parser_find_prefix(current.type);
    if(prefix != NULL) {
        CEGEX_TRY(prefix->parse(ctx, current, prefix->precedence, NULL, &lhs));
    } else {
        CEGEX_TRY(cegex_parser_charcode(ctx, current, CEGEX_PARSING_PRECEDENCE_NONE, NULL, &lhs));
    }

    while(!ITERATOR_EMPTY(&ctx->token_iterator)) {
        CEGEX_TRY(ITERATOR_CURRENT(&ctx->token_iterator, &current));
        if(current.type == CEGEX_TOKEN_PAREN_CLOSE) {
            if(ARRAY_EMPTY(&ctx->group_id_stack)) {
                status = CEGEX_ERR_SYNTAX;
                goto fail;
            }

            break;
        }

        const cegex_parsing_rule* suffix = cegex_parser_find_suffix(current.type);
        if(suffix != NULL && precedence < suffix->precedence) {
            cegex_parse_node* node = NULL;
            status = suffix->parse(ctx, current, suffix->precedence, lhs, &node);

            if(status != CEGEX_OK && current.type != CEGEX_TOKEN_CURLY_OPEN) {
                goto fail;
            } else if(status == CEGEX_OK) {
                lhs = node;
                continue;
            }
        }

        const cegex_parsing_rule* infix = cegex_parser_find_infix(current.type);
        if(infix != NULL) {
            if(precedence > infix->precedence)
                break;

            cegex_parse_node* node = NULL;
            CEGEX_TRY(infix->parse(ctx, current, infix->precedence + 1, lhs, &node));
            lhs = node;

            continue;
        }

        if(cegex_token_starts_primary(current)) {
            if(precedence > CEGEX_PARSING_PRECEDENCE_CONCAT)
                break;

            cegex_parse_node* rhs = NULL;
            CEGEX_TRY(cegex_parser_expr(ctx, CEGEX_PARSING_PRECEDENCE_CONCAT + 1, &rhs));
            lhs = cegex_parse_node_binary(CEGEX_PARSE_NODE_CONCAT, lhs, rhs);

            continue;
        }

        break;
    }

    *out = lhs;
    return CEGEX_OK;
fail:
    return status;
}

cegex_status cegex_parser_exec(cegex_parser* ctx,
                               cegex_tokenized_expr tokenized,
                               cegex_parse_node** out)
{
    assert(ctx != NULL);

    cegex_status status = CEGEX_ERR_UNKNOWN;
    if(tokenized.tokens.length == 0) {
        status = CEGEX_ERR_SYNTAX;
        goto fail;
    }

    if(ctx->num_parsed >= CEGEX_EXPRESSION_CAPACITY) {
        status = CEGEX_ERR_LIMIT;
        goto fail;
    }

    ITERATOR_INIT(&ctx->token_iterator, tokenized.tokens.elements, tokenized.tokens.length);
    ctx->parsed[ctx->num_parsed] = (cegex_parsed_expr){ 0 };
    ctx->parsed[ctx->num_parsed].tokenized = tokenized;

    CEGEX_TRY(cegex_parser_expr(ctx, CEGEX_PARSING_PRECEDENCE_NONE, &ctx->parsed[ctx->num_parsed].root));
    if(out != NULL) {
        *out = ctx->parsed[ctx->num_parsed].root;
    }

    ctx->num_parsed++;
    return CEGEX_OK;
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

    { .code = 127, .repr = "<DEL>" },

    { .code = 128, .repr = "€" },
    { .code = 129, .repr = "<UNUSED>" },

    { .code = 130, .repr = "‚" },
    { .code = 131, .repr = "ƒ" },
    { .code = 132, .repr = "„" },
    { .code = 133, .repr = "…" },
    { .code = 134, .repr = "†" },
    { .code = 135, .repr = "‡" },
    { .code = 136, .repr = "ˆ" },
    { .code = 137, .repr = "‰" },
    { .code = 138, .repr = "Š" },
    { .code = 139, .repr = "‹" },
    { .code = 140, .repr = "Œ" },
    { .code = 141, .repr = "<UNUSED>" },
    { .code = 142, .repr = "Ž" },
    { .code = 143, .repr = "<UNUSED>" },
    { .code = 144, .repr = "<UNUSED>" },
    { .code = 145, .repr = "‘" },
    { .code = 146, .repr = "’" },
    { .code = 147, .repr = "“" },
    { .code = 148, .repr = "”" },
    { .code = 149, .repr = "•" },
    { .code = 150, .repr = "–" },
    { .code = 151, .repr = "—" },
    { .code = 152, .repr = "˜" },
    { .code = 153, .repr = "™" },
    { .code = 154, .repr = "š" },
    { .code = 155, .repr = "›" },
    { .code = 156, .repr = "œ" },
    { .code = 157, .repr = "<UNUSED>" },
    { .code = 158, .repr = "ž" },
    { .code = 159, .repr = "Ÿ" },

    { .code = 160, .repr = "<NBSP>" },
    { .code = 161, .repr = "¡" },
    { .code = 162, .repr = "¢" },
    { .code = 163, .repr = "£" },
    { .code = 164, .repr = "¤" },
    { .code = 165, .repr = "¥" },
    { .code = 166, .repr = "¦" },
    { .code = 167, .repr = "§" },
    { .code = 168, .repr = "¨" },
    { .code = 169, .repr = "©" },
    { .code = 170, .repr = "ª" },
    { .code = 171, .repr = "«" },
    { .code = 172, .repr = "¬" },
    { .code = 173, .repr = "<SHY>" },
    { .code = 174, .repr = "®" },
    { .code = 175, .repr = "¯" },
    { .code = 176, .repr = "°" },
    { .code = 177, .repr = "±" },
    { .code = 178, .repr = "²" },
    { .code = 179, .repr = "³" },
    { .code = 180, .repr = "´" },
    { .code = 181, .repr = "µ" },
    { .code = 182, .repr = "¶" },
    { .code = 183, .repr = "·" },
    { .code = 184, .repr = "¸" },
    { .code = 185, .repr = "¹" },
    { .code = 186, .repr = "º" },
    { .code = 187, .repr = "»" },
    { .code = 188, .repr = "¼" },
    { .code = 189, .repr = "½" },
    { .code = 190, .repr = "¾" },
    { .code = 191, .repr = "¿" },

    { .code = 192, .repr = "À" },
    { .code = 193, .repr = "Á" },
    { .code = 194, .repr = "Â" },
    { .code = 195, .repr = "Ã" },
    { .code = 196, .repr = "Ä" },
    { .code = 197, .repr = "Å" },
    { .code = 198, .repr = "Æ" },
    { .code = 199, .repr = "Ç" },
    { .code = 200, .repr = "È" },
    { .code = 201, .repr = "É" },
    { .code = 202, .repr = "Ê" },
    { .code = 203, .repr = "Ë" },
    { .code = 204, .repr = "Ì" },
    { .code = 205, .repr = "Í" },
    { .code = 206, .repr = "Î" },
    { .code = 207, .repr = "Ï" },
    { .code = 208, .repr = "Ð" },
    { .code = 209, .repr = "Ñ" },
    { .code = 210, .repr = "Ò" },
    { .code = 211, .repr = "Ó" },
    { .code = 212, .repr = "Ô" },
    { .code = 213, .repr = "Õ" },
    { .code = 214, .repr = "Ö" },
    { .code = 215, .repr = "×" },
    { .code = 216, .repr = "Ø" },
    { .code = 217, .repr = "Ù" },
    { .code = 218, .repr = "Ú" },
    { .code = 219, .repr = "Û" },
    { .code = 220, .repr = "Ü" },
    { .code = 221, .repr = "Ý" },
    { .code = 222, .repr = "Þ" },
    { .code = 223, .repr = "ß" },

    { .code = 224, .repr = "à" },
    { .code = 225, .repr = "á" },
    { .code = 226, .repr = "â" },
    { .code = 227, .repr = "ã" },
    { .code = 228, .repr = "ä" },
    { .code = 229, .repr = "å" },
    { .code = 230, .repr = "æ" },
    { .code = 231, .repr = "ç" },
    { .code = 232, .repr = "è" },
    { .code = 233, .repr = "é" },
    { .code = 234, .repr = "ê" },
    { .code = 235, .repr = "ë" },
    { .code = 236, .repr = "ì" },
    { .code = 237, .repr = "í" },
    { .code = 238, .repr = "î" },
    { .code = 239, .repr = "ï" },
    { .code = 240, .repr = "ð" },
    { .code = 241, .repr = "ñ" },
    { .code = 242, .repr = "ò" },
    { .code = 243, .repr = "ó" },
    { .code = 244, .repr = "ô" },
    { .code = 245, .repr = "õ" },
    { .code = 246, .repr = "ö" },
    { .code = 247, .repr = "÷" },
    { .code = 248, .repr = "ø" },
    { .code = 249, .repr = "ù" },
    { .code = 250, .repr = "ú" },
    { .code = 251, .repr = "û" },
    { .code = 252, .repr = "ü" },
    { .code = 253, .repr = "ý" },
    { .code = 254, .repr = "þ" },
    { .code = 255, .repr = "ÿ" }
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
        printf(" <capturing=%s, id=%u>",
               node->data.group.is_capturing ? "true" : "false", node->data.group.index);
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

void cegex_parser_init(cegex_parser* parser)
{
    memset(parser, 0, sizeof(cegex_parser));
    ARRAY_INIT(&parser->group_id_stack);
}

void cegex_parser_free(cegex_parser* parser)
{
    ARRAY_FREE(&parser->group_id_stack);

    for(size_t i = 0; i < parser->num_parsed; i++) {
        cegex_parse_node_free(parser->parsed[i].root);
    }
}

int main(void)
{
    cegex_status status = CEGEX_OK;

    cegex_parser parser;
    cegex_parser_init(&parser);
    cegex_parse_node* root = NULL;

    cegex_tokenizer tokenizer = { 0 };
    cegex_tokenized_expr tokenized = { 0 };
    const char* expr = "[8\\001-\\00499]{1,}?(abc(?:efg))";
    if((status = cegex_tokenizer_exec(&tokenizer, expr, strlen(expr), &tokenized)) != CEGEX_OK) {
        printf("Error occured during tokenization\n");
        goto free;
    }

    if((status = cegex_parser_exec(&parser, tokenized, &root)) != CEGEX_OK) {
        printf("Error occured during parsing\n");
        goto free;
    }

    cegex_parse_node_print(root);

free:
    cegex_parser_free(&parser);
    cegex_tokenizer_free(&tokenizer);
    return status;
}

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

#define CEGEX_PARSER_MAX_TREES         32

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

    CEGEX_PARSE_NODE_QUANTIFIER_PLUS,
    CEGEX_PARSE_NODE_QUANTIFIER_STAR,
    CEGEX_PARSE_NODE_QUANTIFIER_QUANTITY,
    CEGEX_PARSE_NODE_QUANTIFIER_OPTIONAL,
    CEGEX_PARSE_NODE_QUANTIFIER_LAZY,
} cegex_parse_node_type;

typedef struct cegex_parse_node_binary
{
    struct cegex_parse_node* lhs;
    struct cegex_parse_node* rhs;
} cegex_parse_node_alternation, cegex_parse_node_concat;

typedef struct cegex_parse_node_unary
{
    struct cegex_parse_node* value;
} cegex_parse_node_quantified;

typedef struct cegex_character_set
{
    bool negated;
    uchar_iterator characters;
} cegex_character_set;

typedef struct cegex_parse_node
{
    cegex_parse_node_type type;

    union
    {
        struct cegex_character_set set;
        struct cegex_parse_node_unary unary;
        struct cegex_parse_node_binary binary;
    } as;
} cegex_parse_node;

typedef ITERATOR(cegex_token) cegex_token_iterator;
typedef struct cegex_parser
{
    const char* subject;
    cegex_token_iterator token_iterator;
} cegex_parser;

void cegex_parse_node_free(cegex_parse_node* node)
{
    switch(node->type) {
    case CEGEX_PARSE_NODE_CONCAT:
    case CEGEX_PARSE_NODE_ALTERNATE:
        cegex_parse_node_free(node->as.binary.lhs);
        cegex_parse_node_free(node->as.binary.rhs);
        break;

    case CEGEX_PARSE_NODE_QUANTIFIER_PLUS:
    case CEGEX_PARSE_NODE_QUANTIFIER_STAR:
    case CEGEX_PARSE_NODE_QUANTIFIER_QUANTITY:
    case CEGEX_PARSE_NODE_QUANTIFIER_OPTIONAL:
    case CEGEX_PARSE_NODE_QUANTIFIER_LAZY:
        cegex_parse_node_free(node->as.unary.value);
        break;
    default:
        break;
    }

    free(node);
}

cegex_status cegex_parser_expect_tok_typ(cegex_parser* ctx,
                                         cegex_token_type type)
{
    assert(ctx != NULL);

    cegex_token tok;
    ITERATOR_CURRENT(&ctx->token_iterator, &tok);

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
    ITERATOR_CURRENT(&ctx->token_iterator, &tok);

    if((tok.flags & flags) > 0) {
        ITERATOR_NEXT(&ctx->token_iterator);
        return CEGEX_OK;
    }

    return CEGEX_ERR_SYNTAX;
}

cegex_parse_node* cegex_parse_node_charset(
    cegex_parse_node_type type,
    const char* characters,
    size_t length,
    bool negated)
{
    cegex_parse_node* node = malloc(sizeof(cegex_parse_node));
    node->type = type;
    node->as.set.negated = negated;

    ITERATOR_INIT(&node->as.set.characters, characters, length);
    return node;
}

bool cegex_token_starts_primary(cegex_token tok)
{
    switch(tok.type) {
    case CEGEX_TOKEN_ANCHOR_START:
    case CEGEX_TOKEN_ANCHOR_END:
    case CEGEX_TOKEN_CLASS_DOT:
    case CEGEX_TOKEN_PAREN_OPEN:
    case CEGEX_TOKEN_SQUARE_OPEN:
    case CEGEX_TOKEN_CHARACTER:
        return true;

    default:
        break;
    }

    if(tok.flags & TOKEN_ESC) {
        return true;
    }

    return false;
}

cegex_status cegex_parser_primary(cegex_parser* ctx,
                                  cegex_parse_node** out)
{
    cegex_token current;
    ITERATOR_CURRENT(&ctx->token_iterator, &current);
    if(!cegex_token_starts_primary(current)) {
        return CEGEX_ERR_SYNTAX;
    }

    switch(current.type) {
    case CEGEX_TOKEN_CLASS_DOT:
        *out = cegex_parse_node_charset(
            CEGEX_PARSE_NODE_CHARSET,
            "\n\r", 2,
            false);

        cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CLASS_DOT);
        break;

    case CEGEX_TOKEN_CLASS_NOT_WORD:
    case CEGEX_TOKEN_CLASS_WORD:
        bool negated = current.type == CEGEX_TOKEN_CLASS_NOT_WORD;
        *out = cegex_parse_node_charset(
            CEGEX_PARSE_NODE_CHARSET,
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789_",
            63,
            negated);

        cegex_parser_expect_tok_flg(ctx, TOKEN_PERL);
        break;

    case CEGEX_TOKEN_CHARACTER:
        *out = cegex_parse_node_charset(
            CEGEX_PARSE_NODE_CHARSET,
            ctx->subject + current.start, 1,
            false);

        cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CHARACTER);
        break;

    default:
        break;
    }

    return CEGEX_OK;
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

cegex_status cegex_parser_expr(cegex_parser* ctx,
                               uint32_t precedence,
                               cegex_parse_node** out)
{
    cegex_status status = CEGEX_OK;

    cegex_parse_node* lhs;
    cegex_parser_primary(ctx, &lhs);

    while(!ITERATOR_EMPTY(&ctx->token_iterator)) {
        cegex_token current;
        ITERATOR_CURRENT(&ctx->token_iterator, &current);

        if(current.type == CEGEX_TOKEN_ALTERNATE) {
            if(PARSING_PRECEDENCE_ALTERNATION < precedence) {
                break;
            }

            cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_ALTERNATE);

            cegex_parse_node* rhs;
            cegex_parser_expr(ctx, PARSING_PRECEDENCE_ALTERNATION + 1, &rhs);
            lhs = cegex_parse_node_binary(CEGEX_PARSE_NODE_ALTERNATE, lhs, rhs);

        } else if(cegex_token_starts_primary(current)) {
            if(PARSING_PRECEDENCE_CONCAT < precedence) {
                break;
            }

            cegex_parse_node* rhs;
            cegex_parser_expr(ctx, PARSING_PRECEDENCE_CONCAT + 1, &rhs);
            lhs = cegex_parse_node_binary(CEGEX_PARSE_NODE_CONCAT, lhs, rhs);
        }
    }

    *out = lhs;
    return status;
}

cegex_status cegex_parser_exec(cegex_parser* ctx,
                               const char* subject,
                               cegex_token_array tokens,
                               cegex_parse_node** out)
{
    assert(ctx != NULL);
    assert(tokens.length > 0);

    ctx->subject = subject;
    ITERATOR_INIT(&ctx->token_iterator, tokens.elements, tokens.length);
    cegex_parser_expr(ctx, PARSING_PRECEDENCE_NONE, out);

    return CEGEX_OK;
}

int main(void)
{
    cegex_tokenizer tokenizer = { 0 };
    cegex_token_array tokens = { 0 };

    const char* expr = "ab|c";
    if(CEGEX_OK != cegex_tokenizer_exec(&tokenizer, expr, 4, &tokens)) {
        printf("Error occured during tokenization\n");
        cegex_tokenizer_free(&tokenizer);
        return 1;
    }

    cegex_parser parser = { 0 };
    cegex_parse_node* root;
    if(CEGEX_OK != cegex_parser_exec(&parser, expr, tokens, &root)) {
    }

    cegex_parse_node_free(root);

    cegex_tokenizer_free(&tokenizer);
    return 0;
}

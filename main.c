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

    CEGEX_PARSE_NODE_GROUP,

    CEGEX_PARSE_NODE_QUANTIFIER_PLUS,
    CEGEX_PARSE_NODE_QUANTIFIER_STAR,
    CEGEX_PARSE_NODE_QUANTIFIER_QUANTITY,
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
    uchar_iterator characters;
} cegex_character_set;

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
            bool is_exact;
            bool is_range;
            bool is_greedy;

            size_t count_start;
            size_t count_end;
        } quantified;
    } data;

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
    case CEGEX_PARSE_NODE_QUANTIFIER_QUANTITY:
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

/**
 * CharacterSet
 *  ::= "[" [ "^" ] CharacterSetElements "]";
 */
cegex_status cegex_parser_character_set(cegex_parser* ctx,
                                        cegex_parse_node** out)
{
    cegex_character_set set;
    cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_SQUARE_OPEN);

    cegex_token current;
    ITERATOR_CURRENT(&ctx->token_iterator, &current);
    if(current.type == CEGEX_TOKEN_ANCHOR_START) { // TODO: Rename token type
        set.negated = true;
        cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_ANCHOR_START);
    }

    while(!ITERATOR_EMPTY(&ctx->token_iterator)) {
        ITERATOR_CURRENT(&ctx->token_iterator, &current);
        if(current.type == CEGEX_TOKEN_SQUARE_CLOSE) {
            cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_SQUARE_CLOSE);
            break;
        }
    }
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
    case CEGEX_TOKEN_ANCHOR_START:
    case CEGEX_TOKEN_ANCHOR_END:
    case CEGEX_TOKEN_ANCHOR_BOUNDARY:
    case CEGEX_TOKEN_ANCHOR_NOT_BOUNDARY: {
        cegex_parse_node_type node_type = CEGEX_PARSE_NODE_ANCHOR_START;
        node_type = (current.type == CEGEX_TOKEN_ANCHOR_END) ? CEGEX_PARSE_NODE_ANCHOR_END : node_type;
        node_type = (current.type == CEGEX_TOKEN_ANCHOR_BOUNDARY) ? CEGEX_PARSE_NODE_ANCHOR_WORD_BOUNDARY : node_type;
        node_type = (current.type == CEGEX_TOKEN_ANCHOR_NOT_BOUNDARY) ? CEGEX_PARSE_NODE_ANCHOR_NOT_WORD_BOUNDARY : node_type;

        cegex_parser_expect_tok_flg(ctx, TOKEN_SPECIAL);
        *out = cegex_parse_node_empty(node_type);
    } break;

    case CEGEX_TOKEN_SQUARE_OPEN: {
        cegex_parser_character_set(ctx, out);
    } break;

    case CEGEX_TOKEN_CLASS_DOT:
        *out = cegex_parse_node_charset(
            CEGEX_PARSE_NODE_CHARSET,
            "\n\r", 2,
            true);

        cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CLASS_DOT);
        break;

    case CEGEX_TOKEN_CLASS_NOT_WORD:
    case CEGEX_TOKEN_CLASS_WORD: {
        bool negated = current.type == CEGEX_TOKEN_CLASS_NOT_WORD;
        *out = cegex_parse_node_charset(
            CEGEX_PARSE_NODE_CHARSET,
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789_",
            63,
            negated);

        cegex_parser_expect_tok_flg(ctx, TOKEN_PERL);
    } break;

    case CEGEX_TOKEN_CLASS_DIGIT:
    case CEGEX_TOKEN_CLASS_NOT_DIGIT: {
        bool negated = current.type == CEGEX_TOKEN_CLASS_NOT_DIGIT;
        *out = cegex_parse_node_charset(
            CEGEX_PARSE_NODE_CHARSET,
            "0123456789", 10,
            negated);

        cegex_parser_expect_tok_flg(ctx, TOKEN_PERL);
    } break;

    case CEGEX_TOKEN_CLASS_WHITESPACE:
    case CEGEX_TOKEN_CLASS_NOT_WHITESPACE: {
        bool negated = current.type == CEGEX_TOKEN_CLASS_NOT_WHITESPACE;
        *out = cegex_parse_node_charset(
            CEGEX_PARSE_NODE_CHARSET,
            " \n\t", 3,
            negated);

        cegex_parser_expect_tok_flg(ctx, TOKEN_PERL);
    } break;

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
    case CEGEX_PARSE_NODE_QUANTIFIER_QUANTITY:
        return "QUANTITY";
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
           type == CEGEX_PARSE_NODE_QUANTIFIER_QUANTITY ||
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
    printf("negated=%s, characters=[",
           set->negated ? "true" : "false");

    while(!ITERATOR_EMPTY(&set->characters)) {
        uchar current;
        ITERATOR_CURRENT(&set->characters, &current);

        switch(current) {
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

        if(CEGEX_OK == ITERATOR_NEXT(&set->characters)) {
            printf(",");
        }
    }

    printf("]");
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

    case CEGEX_PARSE_NODE_QUANTIFIER_QUANTITY:
        printf(" [");

        if(node->data.quantified.is_exact) {
            printf("%zu", node->data.quantified.count_start);
        } else if(node->data.quantified.is_range) {
            printf("%zu, %zu",
                   node->data.quantified.count_start,
                   node->data.quantified.count_end);
        } else {
            printf("%zu, ...",
                   node->data.quantified.count_start);
        }

        printf(", %s]",
               node->data.quantified.is_greedy ? "greedy" : "lazy");
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
    cegex_tokenizer tokenizer = { 0 };
    cegex_token_array tokens = { 0 };

    const char* expr = "abcd|\\d";
    if(CEGEX_OK != cegex_tokenizer_exec(&tokenizer, expr, strlen(expr), &tokens)) {
        printf("Error occured during tokenization\n");
        cegex_tokenizer_free(&tokenizer);
        return 1;
    }

    cegex_parser parser = { 0 };
    cegex_parse_node* root;
    if(CEGEX_OK != cegex_parser_exec(&parser, expr, tokens, &root)) {
        printf("Error occured during parsing\n");
        cegex_parse_node_free(root);
        return 1;
    }

    cegex_parse_node_print(root);
    cegex_parse_node_free(root);

    cegex_tokenizer_free(&tokenizer);
    return 0;
}

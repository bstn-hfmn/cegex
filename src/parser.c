#include "parser.h"
#include "tokenizer.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define CEGEX_PARSING_PRECEDENCE_NONE        0
#define CEGEX_PARSING_PRECEDENCE_CONCAT      20
#define CEGEX_PARSING_PRECEDENCE_QUANTIFIER  30
#define CEGEX_PARSING_PRECEDENCE_ALTERNATION 10

static bool cegex_token_is_charset_range_rune(cegex_token tok)
{
    if(tok.type == CEGEX_TOKEN_SQUARE_CLOSE ||
       ((tok.flags & CEGEX_TOKEN_FLG_PERL) && tok.type != CEGEX_TOKEN_CLASS_DOT))
        return false;

    return true;
}

static bool cegex_token_is_concatable(cegex_token tok)
{
    if(tok.type == CEGEX_TOKEN_PLUS ||
       tok.type == CEGEX_TOKEN_QUESTION_MARK ||
       tok.type == CEGEX_TOKEN_STAR ||
       tok.type == CEGEX_TOKEN_PAREN_CLOSE ||
       tok.type == CEGEX_TOKEN_ALTERNATE)
        return false;

    return true;
}

/*-------------------- cegex_parse_node --------------------*/
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

cegex_parse_node* cegex_parse_node_charset(
    cegex_charset set)
{
    cegex_parse_node* node = malloc(sizeof(cegex_parse_node));
    node->type = CEGEX_PARSE_NODE_CHARSET;
    node->as.set = set;

    return node;
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

/*-------------------- cegex_parser --------------------*/
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

#define CEGEX_DECL_PARSING_FN(name)                 \
    static cegex_status name(cegex_parser* ctx,     \
                             cegex_token tok,       \
                             uint32_t precedence,   \
                             cegex_parse_node* lhs, \
                             cegex_parse_node** out)

static cegex_status cegex_parser_expr(cegex_parser* ctx,
                                      uint32_t precedence,
                                      cegex_parse_node** out);

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

static const cegex_parsing_rule* cegex_parser_find_prefix(cegex_token_type type)
{
    static const size_t PREFIX_ARRAY_LENGTH = sizeof(prefix_rules) / sizeof(cegex_parsing_rule);
    for(size_t i = 0; i < PREFIX_ARRAY_LENGTH; i++) {
        if(prefix_rules[i].type == type) {
            return &prefix_rules[i];
        }
    }

    return NULL;
}

static const cegex_parsing_rule* cegex_parser_find_suffix(cegex_token_type type)
{
    static const size_t SUFFIX_ARRAY_LENGTH = sizeof(suffix_rules) / sizeof(cegex_parsing_rule);
    for(size_t i = 0; i < SUFFIX_ARRAY_LENGTH; i++) {
        if(suffix_rules[i].type == type) {
            return &suffix_rules[i];
        }
    }

    return NULL;
}

static const cegex_parsing_rule* cegex_parser_find_infix(cegex_token_type type)
{
    static const size_t INFIX_ARRAY_LENGTH = sizeof(infix_rules) / sizeof(cegex_parsing_rule);
    for(size_t i = 0; i < INFIX_ARRAY_LENGTH; i++) {
        if(infix_rules[i].type == type) {
            return &infix_rules[i];
        }
    }

    return NULL;
}

void cegex_parser_init(cegex_parser* parser)
{
    assert(parser != NULL);

    memset(parser, 0, sizeof(cegex_parser));
    ARRAY_INIT(&parser->group_idx_stack);
}

void cegex_parser_free(cegex_parser* parser)
{
    ARRAY_FREE(&parser->group_idx_stack);

    for(size_t i = 0; i < parser->num_parsed; i++) {
        cegex_parse_node_free(parser->parsed[i].root);
    }
}

static void cegex_parser_skip_whitespace(cegex_parser* ctx)
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

static cegex_parsed_expr* cegex_parser_working_expr(cegex_parser* ctx)
{
    assert(ctx != NULL);

    size_t index = ctx->num_parsed;
    if(ctx->num_parsed >= CEGEX_EXPRESSION_CAPACITY) {
        index = ctx->num_parsed - 1;
    }

    return &ctx->parsed[index];
}

static cegex_status cegex_parser_expect_tok_typ(cegex_parser* ctx,
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

static cegex_status cegex_parser_expect_tok_flg(cegex_parser* ctx,
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

static cegex_status cegex_parser_expect_digits(cegex_parser* ctx,
                                               size_t limit,
                                               int radix,
                                               uint64_t* out)
{
    assert(ctx != NULL);
    assert(out != NULL);
    assert(radix == CEGEX_RADIX_DEC ||
           radix == CEGEX_RADIX_HEX ||
           radix == CEGEX_RADIX_OCT);

    cegex_token_flags flags = CEGEX_TOKEN_FLG_DIGIT;
    flags = radix == CEGEX_RADIX_OCT ? CEGEX_TOKEN_FLG_OCT_DIGIT : flags;
    flags = radix == CEGEX_RADIX_HEX ? CEGEX_TOKEN_FLG_HEX_DIGIT : flags;

    size_t num_digits = 0;
    char buffer[CEGEX_DIGIT_BUFFER_LENGTH];

    const cegex_parsed_expr* working = cegex_parser_working_expr(ctx);
    while(!ITERATOR_EMPTY(&ctx->token_iterator)) {
        if(num_digits >= CEGEX_DIGIT_BUFFER_LENGTH - 1)
            return CEGEX_ERR_LIMIT;

        if(limit > 0 && num_digits == limit)
            break;

        cegex_token current;
        ITERATOR_CURRENT(&ctx->token_iterator, &current);

        if(!(current.flags & flags))
            break;

        buffer[num_digits++] = (char) working->tokenized->source[current.start];
        ITERATOR_NEXT(&ctx->token_iterator);
    }

    if(num_digits == 0 || (limit > 0 && num_digits != limit))
        return CEGEX_ERR_UNEXPECTED;

    buffer[num_digits] = '\0';

    char* end;
    errno = 0;
    *out = ((uint64_t) strtoll((const char*) buffer, &end, radix));

    if(errno == ERANGE || errno == EINVAL)
        return CEGEX_ERR_LIMIT;

    return CEGEX_OK;
}

static cegex_status cegex_parser_expect_charcode(cegex_parser* ctx,
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
            CEGEX_TRY(cegex_parser_expect_digits(ctx, 2, CEGEX_RADIX_HEX, &value));

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
                if(CEGEX_OK != (status = cegex_parser_expect_digits(ctx, num_digits, CEGEX_RADIX_OCT, &value)))
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
            *out = working->tokenized->source[tok.start + 1];
        } break;
        }
    } else {
        ITERATOR_NEXT(&ctx->token_iterator);

        assert(tok.length == 1);
        *out = working->tokenized->source[tok.start];
    }

    return CEGEX_OK;

fail:
    return status;
}

static cegex_status cegex_parser_charcode(cegex_parser* ctx,
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

static cegex_status cegex_parser_perl(cegex_parser* ctx,
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
    case CEGEX_TOKEN_CLASS_WORD:
    case CEGEX_TOKEN_CLASS_NOT_WORD:
    case CEGEX_TOKEN_CLASS_DIGIT:
    case CEGEX_TOKEN_CLASS_NOT_DIGIT:
    case CEGEX_TOKEN_CLASS_WHITESPACE:
    case CEGEX_TOKEN_CLASS_NOT_WHITESPACE: {
        CEGEX_TRY(cegex_parser_expect_tok_flg(ctx, CEGEX_TOKEN_FLG_PERL));
        *out = cegex_parse_node_charset(cegex_charset_perl(current.type));
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

static cegex_status cegex_parser_counted_quantifier(cegex_parser* ctx,
                                                    cegex_parse_node* lhs,
                                                    cegex_parse_node** out)
{
    assert(ctx != NULL);
    assert(out != NULL);
    assert(lhs != NULL);

    uint64_t min = 0, max = 0;
    cegex_status status = CEGEX_ERR_UNKNOWN;
    cegex_counted_quantifier_type type;

    CEGEX_TRY(
        cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_CURLY_OPEN));

    cegex_parser_skip_whitespace(ctx);
    CEGEX_TRY(cegex_parser_expect_digits(ctx, 0, CEGEX_RADIX_DEC, &min));

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
            CEGEX_TRY(cegex_parser_expect_digits(ctx, 0, CEGEX_RADIX_DEC, &max));

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

static cegex_status cegex_parser_quantifier(cegex_parser* ctx,
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
        } else if(CEGEX_OK == status) {
            if(cegex_parse_node_is_quantifier(lhs))
                return CEGEX_ERR_SYNTAX;
        }
    } else {
        if(cegex_parse_node_is_quantifier(lhs))
            return CEGEX_ERR_SYNTAX;

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

static cegex_status cegex_parser_alternate(cegex_parser* ctx,
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

static cegex_status cegex_parser_character_set(cegex_parser* ctx,
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

    cegex_charset set = { 0 };
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
                CEGEX_TRY(cegex_charset_add_range(&set, low, high));
                goto next;
            }
        }

        cegex_uchar charcode;
        CEGEX_TRY(cegex_parser_expect_charcode(ctx, &charcode));

        if(current.flags & CEGEX_TOKEN_FLG_ESC) {
            if((current.flags & CEGEX_TOKEN_FLG_PERL) && current.type != CEGEX_TOKEN_CLASS_DOT) {
                CEGEX_TRY(cegex_charset_add_subset(&set, cegex_charset_perl(current.type)));
            } else {
                CEGEX_TRY(cegex_charset_add(&set, charcode));
            }
        } else {
            assert(current.length == 1);
            CEGEX_TRY(cegex_charset_add(&set, charcode));
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

static cegex_status cegex_parser_group(cegex_parser* ctx,
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

    CEGEX_TRY(
        ARRAY_PUSH(&ctx->group_idx_stack, working->num_groups));

    if(is_capturing)
        working->num_groups++;

    cegex_parse_node* expr = NULL;
    status = cegex_parser_expr(ctx, CEGEX_PARSING_PRECEDENCE_NONE, &expr);

    uint32_t group_index;
    ARRAY_POP(&ctx->group_idx_stack, &group_index);

    if(status != CEGEX_OK)
        goto fail;

    if(CEGEX_OK != (status = cegex_parser_expect_tok_typ(ctx, CEGEX_TOKEN_PAREN_CLOSE))) {
        cegex_parse_node_free(expr);
        goto fail;
    }

    cegex_parse_node* node = cegex_parse_node_unary(CEGEX_PARSE_NODE_GROUP, expr);
    node->data.group.is_capturing = is_capturing;
    node->data.group.index = is_capturing ? group_index : 0;

    *out = node;
    return CEGEX_OK;
fail:
    return status;
}

static cegex_status cegex_parser_anchor(cegex_parser* ctx,
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

static cegex_status cegex_parser_expr(cegex_parser* ctx,
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
    } else if(cegex_token_is_concatable(current)) {
        CEGEX_TRY(cegex_parser_charcode(ctx, current, CEGEX_PARSING_PRECEDENCE_NONE, NULL, &lhs));
    } else {
        status = CEGEX_ERR_UNEXPECTED;
        goto fail;
    }

    while(!ITERATOR_EMPTY(&ctx->token_iterator)) {
        CEGEX_TRY(ITERATOR_CURRENT(&ctx->token_iterator, &current));
        if(current.type == CEGEX_TOKEN_PAREN_CLOSE) {
            if(ARRAY_EMPTY(&ctx->group_idx_stack)) {
                status = CEGEX_ERR_SYNTAX;
                goto fail;
            }

            break;
        }

        const cegex_parsing_rule* suffix = cegex_parser_find_suffix(current.type);
        if(suffix != NULL && precedence < suffix->precedence) {
            cegex_parse_node* node = NULL;
            status = suffix->parse(ctx, current, suffix->precedence, lhs, &node);

            if(status == CEGEX_OK) {
                lhs = node;
                continue;
            }
            if(!(current.type == CEGEX_TOKEN_CURLY_OPEN && status == CEGEX_ERR_UNEXPECTED))
                goto fail;
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

        if(cegex_token_is_concatable(current)) {
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
    cegex_parse_node_free(lhs);
    return status;
}

cegex_status cegex_parser_exec(cegex_parser* ctx,
                               cegex_tokenized_expr* tokenized,
                               cegex_parsed_expr** out)
{
    assert(ctx != NULL);
    cegex_status status = CEGEX_ERR_UNKNOWN;
    if(tokenized->tokens.length == 0) {
        status = CEGEX_ERR_SYNTAX;
        goto fail;
    }

    if(ctx->num_parsed >= CEGEX_EXPRESSION_CAPACITY) {
        status = CEGEX_ERR_LIMIT;
        goto fail;
    }

    ITERATOR_INIT(&ctx->token_iterator, tokenized->tokens.elements, tokenized->tokens.length);
    ctx->parsed[ctx->num_parsed] = (cegex_parsed_expr){ 0 };
    ctx->parsed[ctx->num_parsed].tokenized = tokenized;
    ctx->group_idx_stack.length = 0;

    CEGEX_TRY(cegex_parser_expr(ctx, CEGEX_PARSING_PRECEDENCE_NONE, &ctx->parsed[ctx->num_parsed].root));
    assert(ITERATOR_EMPTY(&ctx->token_iterator));

    if(out != NULL) {
        *out = &ctx->parsed[ctx->num_parsed];
    }

    ctx->num_parsed++;
    return CEGEX_OK;
fail:
    return status;
}

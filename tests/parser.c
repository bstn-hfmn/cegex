#include "../src/parser.h"
#include "../src/tokenizer.h"
#include "testing.h"
#include <string.h>

#define NODE_IS(n, t)       is_node_type((n), (t))
#define NODE_CHARCODE(n, c) is_node_charcode((n), (c))

#define LHS(n)              ((n)->as.binary.lhs)
#define RHS(n)              ((n)->as.binary.rhs)
#define VAL(n)              ((n)->as.unary.value)

#define BEGIN_EXPR(pattern)                               \
    fixture_parse(&fixture, pattern);                     \
    EXPECT_EQ_GOTO(fixture.status, CEGEX_OK, cleanup);    \
    if(fixture.parsed->root == NULL) {                    \
        REPORT_FAILURE("root is NULL for '%s'", pattern); \
        goto cleanup;                                     \
    }

#define BEGIN_FIXTURE            \
    static test_fixture fixture; \
    fixture_init(&fixture);

#define END_FIXTURE \
    cleanup:        \
    fixture_free(&fixture)

#define EXPECT_REJECTED(pattern) \
    EXPECT_TRUE(status_of(pattern) != CEGEX_OK)

typedef struct test_fixture
{
    cegex_parser parser;
    cegex_tokenizer tokenizer;
    cegex_parsed_expr* parsed;
    cegex_tokenized_expr* tokenized;

    cegex_status status;
} test_fixture;

static void fixture_init(test_fixture* fixture)
{
    memset(fixture, 0, sizeof(test_fixture));

    cegex_tokenizer_init(&fixture->tokenizer);
    cegex_parser_init(&fixture->parser);
}

static void fixture_free(test_fixture* fixture)
{
    cegex_tokenizer_free(&fixture->tokenizer);
    cegex_parser_free(&fixture->parser);
}

static cegex_status fixture_parse(test_fixture* fixture,
                                  const char* regex)
{
    fixture->parsed = NULL;
    fixture->tokenized = NULL;

    fixture->status = cegex_tokenizer_exec(&fixture->tokenizer, regex, strlen(regex), &fixture->tokenized);
    if(CEGEX_OK == fixture->status) {
        fixture->status = cegex_parser_exec(&fixture->parser, fixture->tokenized, &fixture->parsed);
    }

    return fixture->status;
}

static cegex_status status_of(const char* pattern)
{
    static test_fixture fixture;
    fixture_init(&fixture);
    fixture_parse(&fixture, pattern);

    cegex_status status = fixture.status;
    fixture_free(&fixture);
    return status;
}

static bool is_node_type(const cegex_parse_node* node,
                         cegex_parse_node_type type)
{
    return node != NULL && node->type == type;
}

static bool is_node_charcode(const cegex_parse_node* node,
                             cegex_uchar charcode)
{
    return node != NULL &&
           node->type == CEGEX_PARSE_NODE_CHARSET &&
           node->as.set.num_characters == 1 &&
           node->as.set.characters[0] == charcode;
}

static void test_single_charcode(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("a");
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, 'a'));
    EXPECT_EQ(fixture.parser.num_parsed, 1);
    EXPECT_EQ(fixture.parsed->num_groups, 0);

    END_FIXTURE;
}

static void test_concat_is_left_assoc(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("abc");
    const cegex_parse_node* root = fixture.parsed->root;

    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_CONCAT) &&
                NODE_IS(LHS(root), CEGEX_PARSE_NODE_CONCAT) &&
                NODE_CHARCODE(LHS(LHS(root)), 'a') &&
                NODE_CHARCODE(RHS(LHS(root)), 'b') &&
                NODE_CHARCODE(RHS(root), 'c'));

    END_FIXTURE;
}

static void test_alternation_simple(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("a|b");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_ALTERNATE) &&
                NODE_CHARCODE(LHS(root), 'a') &&
                NODE_CHARCODE(RHS(root), 'b'));

    END_FIXTURE;
}

static void test_alternation_chain_is_left_assoc(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("a|b|c");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_ALTERNATE) &&
                NODE_IS(LHS(root), CEGEX_PARSE_NODE_ALTERNATE) &&
                NODE_CHARCODE(LHS(LHS(root)), 'a') &&
                NODE_CHARCODE(RHS(LHS(root)), 'b') &&
                NODE_CHARCODE(RHS(root), 'c'));
    END_FIXTURE;
}

static void test_concat_binds_tighter_than_alternation(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("ab|cd");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_ALTERNATE) &&
                NODE_IS(LHS(root), CEGEX_PARSE_NODE_CONCAT) &&
                NODE_IS(RHS(root), CEGEX_PARSE_NODE_CONCAT) &&
                NODE_CHARCODE(LHS(LHS(root)), 'a') && NODE_CHARCODE(RHS(LHS(root)), 'b') &&
                NODE_CHARCODE(LHS(RHS(root)), 'c') && NODE_CHARCODE(RHS(RHS(root)), 'd'));

    END_FIXTURE;
}

static void test_alternation_rhs_concat(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("a|bc");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_ALTERNATE) &&
                NODE_CHARCODE(LHS(root), 'a') &&
                NODE_IS(RHS(root), CEGEX_PARSE_NODE_CONCAT) &&
                NODE_CHARCODE(LHS(RHS(root)), 'b') &&
                NODE_CHARCODE(RHS(RHS(root)), 'c'));
    END_FIXTURE;
}

static void test_dot(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR(".");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_CHARSET) &&
                root->as.set.negated &&
                cegex_charset_contains(&root->as.set, '\n') &&
                cegex_charset_contains(&root->as.set, '\r'));
    END_FIXTURE;
}

static void test_digit_class(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("\\d");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_CHARSET) &&
                !root->as.set.negated &&
                cegex_charset_contains(&root->as.set, '0') &&
                cegex_charset_contains(&root->as.set, '9') &&
                !cegex_charset_contains(&root->as.set, 'a'));
    END_FIXTURE;
}

static void test_word_class(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("\\w");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_CHARSET) &&
                !root->as.set.negated &&
                cegex_charset_contains(&root->as.set, 'a') &&
                cegex_charset_contains(&root->as.set, '9') &&
                cegex_charset_contains(&root->as.set, '_') &&
                !cegex_charset_contains(&root->as.set, '-'));
    END_FIXTURE;
}

static void test_whitespace_class(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("\\s");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_CHARSET) &&
                !root->as.set.negated &&
                cegex_charset_contains(&root->as.set, ' ') &&
                cegex_charset_contains(&root->as.set, '\t') &&
                !cegex_charset_contains(&root->as.set, 'a'));

    END_FIXTURE;
}

static void test_escaped_meta_literals(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("\\.");
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '.'));

    BEGIN_EXPR("\\*");
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '*'));

    BEGIN_EXPR("\\(");
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '('));

    BEGIN_EXPR("\\\\");
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '\\'));

    BEGIN_EXPR("\\[");
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '['));

    END_FIXTURE;
}

static void test_escaped_ctrl(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("\\n");
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '\n'));

    BEGIN_EXPR("\\t");
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '\t'));

    BEGIN_EXPR("\\r");
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '\r'));

    END_FIXTURE;
}

static void test_hex_escape(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("\\x41");
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, 'A'));

    END_FIXTURE;
}

static void test_hex_escape_followed_by_charcode(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("\\x41b");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_CONCAT) &&
                NODE_CHARCODE(LHS(root), 'A') &&
                NODE_CHARCODE(RHS(root), 'b'));

    END_FIXTURE;
}

static void test_octal_escape(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("\\101");
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, 'A'));

    BEGIN_EXPR("\\61");
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '1'));

    BEGIN_EXPR("\\61b");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_CONCAT) &&
                NODE_CHARCODE(LHS(root), '1') &&
                NODE_CHARCODE(RHS(root), 'b'));

    END_FIXTURE;
}

static void test_incomplete_hex_escape_rejected(void)
{
    EXPECT_REJECTED("\\x4");
    EXPECT_REJECTED("\\x");
}

static void test_capturing_group(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("(a)");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_GROUP) &&
                root->data.group.is_capturing &&
                NODE_CHARCODE(VAL(root), 'a'));

    EXPECT_TRUE(fixture.parsed->num_groups == 1);

    END_FIXTURE;
}

static void test_non_capturing_group(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("(?:a)");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_GROUP) && !root->data.group.is_capturing && NODE_CHARCODE(VAL(root), 'a'));
    EXPECT_TRUE(fixture.parsed->num_groups == 0);

    END_FIXTURE;
}

static void test_nested_group_numbering(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("((a)(b))");
    const cegex_parse_node* outer = fixture.parsed->root;
    EXPECT_TRUE(fixture.parsed->num_groups == 3);
    EXPECT_TRUE(NODE_IS(outer, CEGEX_PARSE_NODE_GROUP) && NODE_IS(VAL(outer), CEGEX_PARSE_NODE_CONCAT));
    if(NODE_IS(outer, CEGEX_PARSE_NODE_GROUP) && NODE_IS(VAL(outer), CEGEX_PARSE_NODE_CONCAT)) {
        const cegex_parse_node* a = LHS(VAL(outer));
        const cegex_parse_node* b = RHS(VAL(outer));

        EXPECT_TRUE(NODE_IS(a, CEGEX_PARSE_NODE_GROUP) && NODE_IS(b, CEGEX_PARSE_NODE_GROUP));
        if(NODE_IS(a, CEGEX_PARSE_NODE_GROUP) && NODE_IS(b, CEGEX_PARSE_NODE_GROUP)) {
            EXPECT_TRUE(outer->data.group.index < a->data.group.index);
            EXPECT_TRUE(a->data.group.index < b->data.group.index);
            EXPECT_TRUE(b->data.group.index < fixture.parsed->num_groups);
        }
    }

    END_FIXTURE;
}

static void test_non_capturing_groups_do_not_consume_indices(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("(a)(?:b)(c)");
    EXPECT_TRUE(fixture.parsed->num_groups == 2);
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_CONCAT) &&
                NODE_IS(LHS(root), CEGEX_PARSE_NODE_CONCAT) &&
                NODE_IS(RHS(root), CEGEX_PARSE_NODE_GROUP) &&
                NODE_IS(LHS(LHS(root)), CEGEX_PARSE_NODE_GROUP) &&
                NODE_IS(RHS(LHS(root)), CEGEX_PARSE_NODE_GROUP));

    if(NODE_IS(root, CEGEX_PARSE_NODE_CONCAT) &&
       NODE_IS(LHS(root), CEGEX_PARSE_NODE_CONCAT) &&
       NODE_IS(RHS(root), CEGEX_PARSE_NODE_GROUP) &&
       NODE_IS(LHS(LHS(root)), CEGEX_PARSE_NODE_GROUP) && NODE_IS(RHS(LHS(root)), CEGEX_PARSE_NODE_GROUP)) {

        EXPECT_TRUE(LHS(LHS(root))->data.group.is_capturing);
        EXPECT_TRUE(!RHS(LHS(root))->data.group.is_capturing);
        EXPECT_TRUE(RHS(root)->data.group.is_capturing);
        EXPECT_TRUE(LHS(LHS(root))->data.group.index != RHS(root)->data.group.index);
    }

    END_FIXTURE;
}

static void test_unbalanced_parentheses_rejected(void)
{
    EXPECT_REJECTED("(a");
    EXPECT_REJECTED("((a)");
    EXPECT_REJECTED("a)");
    EXPECT_REJECTED("(a))");
    EXPECT_REJECTED("(");
    EXPECT_REJECTED("(?:");
    EXPECT_REJECTED("(?a)");
}

static void test_lone_close_paren_rejected(void)
{
    EXPECT_REJECTED(")");
    EXPECT_REJECTED(")a");
    EXPECT_REJECTED("a|)");
}

static void test_empty_group_rejected(void)
{
    EXPECT_REJECTED("()");
    EXPECT_REJECTED("(?:)");
}

static void test_star_plus_optional(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("a*");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_QUANTIFIER_STAR) && NODE_CHARCODE(VAL(fixture.parsed->root), 'a'));

    BEGIN_EXPR("a+");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_QUANTIFIER_PLUS) && NODE_CHARCODE(VAL(fixture.parsed->root), 'a'));

    BEGIN_EXPR("a?");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_QUANTIFIER_OPTIONAL) && NODE_CHARCODE(VAL(fixture.parsed->root), 'a'));

    END_FIXTURE;
}

static void test_quantifier_binds_to_last_atom(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("ab*");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_CONCAT) &&
                NODE_CHARCODE(LHS(r), 'a') &&
                NODE_IS(RHS(r), CEGEX_PARSE_NODE_QUANTIFIER_STAR) &&
                NODE_CHARCODE(VAL(RHS(r)), 'b'));

    END_FIXTURE;
}

static void test_quantifier_followed_by_atom(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("a*b");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_CONCAT) &&
                NODE_IS(LHS(r), CEGEX_PARSE_NODE_QUANTIFIER_STAR) &&
                NODE_CHARCODE(RHS(r), 'b'));

    END_FIXTURE;
}

static void test_quantified_group_and_set(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("(ab)+");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_QUANTIFIER_PLUS) &&
                NODE_IS(VAL(fixture.parsed->root), CEGEX_PARSE_NODE_GROUP));

    BEGIN_EXPR("[ab]*");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_QUANTIFIER_STAR) &&
                NODE_IS(VAL(fixture.parsed->root), CEGEX_PARSE_NODE_CHARSET));

    END_FIXTURE;
}

static void test_quantifier_inside_alternation(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("a*|b+");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_ALTERNATE) &&
                NODE_IS(LHS(r), CEGEX_PARSE_NODE_QUANTIFIER_STAR) &&
                NODE_IS(RHS(r), CEGEX_PARSE_NODE_QUANTIFIER_PLUS));
    END_FIXTURE;
}

static void test_lazy_quantifiers(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("a*?");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_QUANTIFIER_LAZY) && NODE_IS(VAL(fixture.parsed->root), CEGEX_PARSE_NODE_QUANTIFIER_STAR));

    BEGIN_EXPR("a+?");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_QUANTIFIER_LAZY) && NODE_IS(VAL(fixture.parsed->root), CEGEX_PARSE_NODE_QUANTIFIER_PLUS));

    BEGIN_EXPR("a??");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_QUANTIFIER_LAZY) && NODE_IS(VAL(fixture.parsed->root), CEGEX_PARSE_NODE_QUANTIFIER_OPTIONAL));

    BEGIN_EXPR("a{2}?");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_QUANTIFIER_LAZY) && NODE_IS(VAL(fixture.parsed->root), CEGEX_PARSE_NODE_QUANTIFIER_COUNTED));
    END_FIXTURE;
}

static void test_stacked_quantifiers_rejected(void)
{
    EXPECT_REJECTED("a**");
    EXPECT_REJECTED("a+*");
    EXPECT_REJECTED("a*??");
    EXPECT_REJECTED("a{2}*");
}

static void test_counted_exact(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("a{3}");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_QUANTIFIER_COUNTED) && NODE_CHARCODE(VAL(r), 'a'));
    if(NODE_IS(r, CEGEX_PARSE_NODE_QUANTIFIER_COUNTED)) {
        EXPECT_TRUE(r->data.counted.type == CEGEX_COUNTED_QUANTIFIER_EXACT);
        EXPECT_TRUE(r->data.counted.count == 3);
    }
    END_FIXTURE;
}

static void test_counted_multi_digit(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("a{12}");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_QUANTIFIER_COUNTED) && r->data.counted.count == 12);
    END_FIXTURE;
}

static void test_counted_zero(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("a{0}");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_QUANTIFIER_COUNTED) && r->data.counted.count == 0);
    END_FIXTURE;
}

static void test_counted_min(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("a{2,}");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_QUANTIFIER_COUNTED));
    if(NODE_IS(r, CEGEX_PARSE_NODE_QUANTIFIER_COUNTED)) {
        EXPECT_TRUE(r->data.counted.type == CEGEX_COUNTED_QUANTIFIER_MIN);
        EXPECT_TRUE(r->data.counted.min == 2);
    }
    END_FIXTURE;
}

static void test_counted_min_max(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("a{2,5}");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_QUANTIFIER_COUNTED));
    if(NODE_IS(r, CEGEX_PARSE_NODE_QUANTIFIER_COUNTED)) {
        EXPECT_TRUE(r->data.counted.type == CEGEX_COUNTED_QUANTIFIER_MIN_MAX);
        EXPECT_TRUE(r->data.counted.min == 2);
        EXPECT_TRUE(r->data.counted.max == 5);
    }
    END_FIXTURE;
}

static void test_counted_min_equals_max(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("a{3,3}");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_QUANTIFIER_COUNTED) && r->data.counted.min == 3 && r->data.counted.max == 3);

    END_FIXTURE;
}

static void test_counted_on_group_and_binding(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("(ab){2}");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_QUANTIFIER_COUNTED) && NODE_IS(VAL(fixture.parsed->root), CEGEX_PARSE_NODE_GROUP));

    BEGIN_EXPR("ab{2}");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_CONCAT) && NODE_CHARCODE(LHS(r), 'a') && NODE_IS(RHS(r), CEGEX_PARSE_NODE_QUANTIFIER_COUNTED));

    END_FIXTURE;
}

static void test_counted_min_greater_than_max_rejected(void)
{
    cegex_status s = status_of("a{3,2}");
    EXPECT_EQ(s, CEGEX_ERR_SYNTAX);
}

static void test_counted_stacked_rejected(void)
{
    cegex_status s = status_of("a{2}{3}");
    EXPECT_EQ(s, CEGEX_ERR_SYNTAX);
}

static void test_counted_overflowing_number_rejected(void)
{
    EXPECT_REJECTED("a{99999999999999999999999999999999999}");
    EXPECT_REJECTED("a{99999999999999999999}");
}

static void test_brace_literal_fallback(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("a{");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_CONCAT) &&
                NODE_CHARCODE(LHS(fixture.parsed->root), 'a') &&
                NODE_CHARCODE(RHS(fixture.parsed->root), '{'));

    BEGIN_EXPR("a{,3}");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_CONCAT));

    BEGIN_EXPR("a{x}");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_CONCAT));

    BEGIN_EXPR("a{3");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_CONCAT));

    BEGIN_EXPR("{");
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '{'));

    END_FIXTURE;
}

static void test_brace_literal_after_quantifier(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("a+{");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_CONCAT) &&
                NODE_IS(LHS(fixture.parsed->root), CEGEX_PARSE_NODE_QUANTIFIER_PLUS) &&
                NODE_CHARCODE(RHS(fixture.parsed->root), '{'));

    END_FIXTURE;
}

static void test_anchors(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("^a$");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_CONCAT) &&
                NODE_IS(LHS(fixture.parsed->root), CEGEX_PARSE_NODE_CONCAT) &&
                NODE_IS(LHS(LHS(fixture.parsed->root)), CEGEX_PARSE_NODE_ANCHOR_START) &&
                NODE_CHARCODE(RHS(LHS(fixture.parsed->root)), 'a') &&
                NODE_IS(RHS(fixture.parsed->root), CEGEX_PARSE_NODE_ANCHOR_END));

    BEGIN_EXPR("\\bx");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_CONCAT) && NODE_IS(LHS(fixture.parsed->root), CEGEX_PARSE_NODE_ANCHOR_WORD_BOUNDARY));

    BEGIN_EXPR("\\Bx");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_CONCAT) && NODE_IS(LHS(fixture.parsed->root), CEGEX_PARSE_NODE_ANCHOR_NOT_WORD_BOUNDARY));

    END_FIXTURE;
}

static void test_anchors_inside_alternation_in_group(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("(^a|b$)");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_GROUP) &&
                NODE_IS(VAL(r), CEGEX_PARSE_NODE_ALTERNATE) &&
                NODE_IS(LHS(VAL(r)), CEGEX_PARSE_NODE_CONCAT) &&
                NODE_IS(LHS(LHS(VAL(r))), CEGEX_PARSE_NODE_ANCHOR_START) &&
                NODE_IS(RHS(VAL(r)), CEGEX_PARSE_NODE_CONCAT) &&
                NODE_IS(RHS(RHS(VAL(r))), CEGEX_PARSE_NODE_ANCHOR_END));

    END_FIXTURE;
}

static void test_trailing_operators_rejected(void)
{
    EXPECT_REJECTED("a|");
    EXPECT_REJECTED("(a|)");
    EXPECT_REJECTED("a|b|");
}

static void test_set_simple(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("[abc]");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_CHARSET) && cegex_charset_contains(&r->as.set, 'a') && cegex_charset_contains(&r->as.set, 'b') &&
                cegex_charset_contains(&r->as.set, 'c') && !cegex_charset_contains(&r->as.set, 'd'));
    END_FIXTURE;
}

static void test_set_range(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("[a-c]");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_CHARSET) && cegex_charset_contains(&r->as.set, 'a') && cegex_charset_contains(&r->as.set, 'b') &&
                cegex_charset_contains(&r->as.set, 'c') && !cegex_charset_contains(&r->as.set, 'd') && !cegex_charset_contains(&r->as.set, '-'));

    END_FIXTURE;
}

static void test_set_multiple_ranges(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("[a-cx-z0-2]");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_CHARSET) && cegex_charset_contains(&r->as.set, 'b') && cegex_charset_contains(&r->as.set, 'y') && cegex_charset_contains(&r->as.set, '1') &&
                !cegex_charset_contains(&r->as.set, 'm') && !cegex_charset_contains(&r->as.set, '5'));
    END_FIXTURE;
}

static void test_set_negated(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("[^a]");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_CHARSET) && fixture.parsed->root->as.set.negated);

    END_FIXTURE;
}

static void test_set_hat_not_first_is_literal(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("[a^]");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_CHARSET) && !r->as.set.negated && cegex_charset_contains(&r->as.set, '^') && cegex_charset_contains(&r->as.set, 'a'));

    END_FIXTURE;
}

static void test_set_dash_literals(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("[a-]");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_CHARSET) && cegex_charset_contains(&fixture.parsed->root->as.set, 'a') && cegex_charset_contains(&fixture.parsed->root->as.set, '-') &&
                !cegex_charset_contains(&fixture.parsed->root->as.set, 'b'));
    BEGIN_EXPR("[-a]");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_CHARSET) && cegex_charset_contains(&fixture.parsed->root->as.set, 'a') && cegex_charset_contains(&fixture.parsed->root->as.set, '-'));

    BEGIN_EXPR("[a-c-e]");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_CHARSET) && cegex_charset_contains(&r->as.set, 'b') && cegex_charset_contains(&r->as.set, '-') &&
                cegex_charset_contains(&r->as.set, 'e') && !cegex_charset_contains(&r->as.set, 'd'));

    END_FIXTURE;
}

static void test_set_with_perl_class(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("[\\d_]");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_CHARSET) && cegex_charset_contains(&r->as.set, '5') && cegex_charset_contains(&r->as.set, '_') && !cegex_charset_contains(&r->as.set, 'a'));

    END_FIXTURE;
}

static void test_set_dash_after_perl_class_is_literal(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("[\\d-z]");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_CHARSET) && cegex_charset_contains(&r->as.set, '5') && cegex_charset_contains(&r->as.set, '-') &&
                cegex_charset_contains(&r->as.set, 'z') && !cegex_charset_contains(&r->as.set, 'a'));
    END_FIXTURE;
}

static void test_set_dot_is_literal(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("[.]");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_CHARSET) && cegex_charset_contains(&fixture.parsed->root->as.set, '.') && !cegex_charset_contains(&fixture.parsed->root->as.set, 'a'));

    END_FIXTURE;
}

static void test_set_escapes(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("[\\n\\t]");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_CHARSET) && cegex_charset_contains(&fixture.parsed->root->as.set, '\n') && cegex_charset_contains(&fixture.parsed->root->as.set, '\t'));

    BEGIN_EXPR("[\\]]");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_CHARSET) && cegex_charset_contains(&fixture.parsed->root->as.set, ']'));

    END_FIXTURE;
}

static void test_set_range_with_hex_endpoints(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("[\\x41-\\x43]");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_CHARSET) && cegex_charset_contains(&r->as.set, 'A') && cegex_charset_contains(&r->as.set, 'B') &&
                cegex_charset_contains(&r->as.set, 'C') && !cegex_charset_contains(&r->as.set, 'D'));

    END_FIXTURE;
}

static void test_set_in_context(void)
{
    BEGIN_FIXTURE;

    BEGIN_EXPR("x[a-c]y");
    const cegex_parse_node* r = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(r, CEGEX_PARSE_NODE_CONCAT) && NODE_IS(LHS(r), CEGEX_PARSE_NODE_CONCAT) && NODE_IS(RHS(LHS(r)), CEGEX_PARSE_NODE_CHARSET) && NODE_CHARCODE(RHS(r), 'y'));

    BEGIN_EXPR("[a-z]+");
    EXPECT_TRUE(NODE_IS(fixture.parsed->root, CEGEX_PARSE_NODE_QUANTIFIER_PLUS) && NODE_IS(VAL(fixture.parsed->root), CEGEX_PARSE_NODE_CHARSET));

    END_FIXTURE;
}

static void test_set_errors(void)
{
    EXPECT_REJECTED("[]");
    EXPECT_REJECTED("[^]");
    EXPECT_REJECTED("[");
    EXPECT_REJECTED("[abc");
    EXPECT_REJECTED("[a-z");
    EXPECT_REJECTED("[z-a]");
}

static void test_realistic_patterns_parse(void)
{
    static const char* const patterns[] = {
        "^[a-zA-Z_][a-zA-Z0-9_]*$",
        "(\\d{1,3}\\.){3}\\d{1,3}",
        "\\b(?:foo|bar)+\\b",
        "[^\\s,]+(,[^\\s,]+)*",
        "colou?r",
        "<.+?>",
        "(a|b|c|d|e)*(x|y)?z+",
    };

    for(size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
        cegex_status status = status_of(patterns[i]);
        if(status != CEGEX_OK)
            REPORT_FAILURE("pattern '%s' was rejected", patterns[i]);
    }
}

int main(void)
{
    RUN_TEST(test_single_charcode);
    RUN_TEST(test_concat_is_left_assoc);
    RUN_TEST(test_alternation_simple);
    RUN_TEST(test_alternation_chain_is_left_assoc);
    RUN_TEST(test_concat_binds_tighter_than_alternation);
    RUN_TEST(test_alternation_rhs_concat);

    RUN_TEST(test_dot);
    RUN_TEST(test_digit_class);
    RUN_TEST(test_word_class);
    RUN_TEST(test_whitespace_class);

    RUN_TEST(test_escaped_meta_literals);
    RUN_TEST(test_escaped_ctrl);
    RUN_TEST(test_hex_escape);
    RUN_TEST(test_octal_escape);
    RUN_TEST(test_hex_escape_followed_by_charcode);
    RUN_TEST(test_incomplete_hex_escape_rejected);

    RUN_TEST(test_capturing_group);
    RUN_TEST(test_non_capturing_group);
    RUN_TEST(test_nested_group_numbering);
    RUN_TEST(test_non_capturing_groups_do_not_consume_indices);
    RUN_TEST(test_unbalanced_parentheses_rejected);
    RUN_TEST(test_lone_close_paren_rejected);
    RUN_TEST(test_empty_group_rejected);

    RUN_TEST(test_star_plus_optional);
    RUN_TEST(test_quantifier_binds_to_last_atom);
    RUN_TEST(test_quantifier_followed_by_atom);
    RUN_TEST(test_quantified_group_and_set);
    RUN_TEST(test_quantifier_inside_alternation);
    RUN_TEST(test_lazy_quantifiers);
    RUN_TEST(test_stacked_quantifiers_rejected);

    RUN_TEST(test_counted_exact);
    RUN_TEST(test_counted_multi_digit);
    RUN_TEST(test_counted_zero);
    RUN_TEST(test_counted_min);
    RUN_TEST(test_counted_min_max);
    RUN_TEST(test_counted_min_equals_max);
    RUN_TEST(test_counted_on_group_and_binding);
    RUN_TEST(test_counted_min_greater_than_max_rejected);
    RUN_TEST(test_counted_stacked_rejected);

    RUN_TEST(test_counted_overflowing_number_rejected);

    RUN_TEST(test_brace_literal_fallback);
    RUN_TEST(test_brace_literal_after_quantifier);

    RUN_TEST(test_anchors);
    RUN_TEST(test_anchors_inside_alternation_in_group);
    RUN_TEST(test_trailing_operators_rejected);

    RUN_TEST(test_set_simple);
    RUN_TEST(test_set_range);
    RUN_TEST(test_set_multiple_ranges);
    RUN_TEST(test_set_negated);
    RUN_TEST(test_set_with_perl_class);
    RUN_TEST(test_set_hat_not_first_is_literal);
    RUN_TEST(test_set_dash_literals);
    RUN_TEST(test_set_dash_after_perl_class_is_literal);
    RUN_TEST(test_set_dot_is_literal);
    RUN_TEST(test_set_escapes);
    RUN_TEST(test_set_range_with_hex_endpoints);
    RUN_TEST(test_set_in_context);
    RUN_TEST(test_set_errors);

    RUN_TEST(test_realistic_patterns_parse);

    return print_summary();
}
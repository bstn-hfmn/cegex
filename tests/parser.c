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
    static test_fixture fixture;                          \
    fixture_init(&fixture);                               \
    fixture_parse(&fixture, pattern);                     \
    EXPECT_EQ_GOTO(fixture.status, CEGEX_OK, cleanup);    \
    if(fixture.parsed->root == NULL) {                    \
        REPORT_FAILURE("root is NULL for '%s'", pattern); \
        goto cleanup;                                     \
    }

#define END_EXPR \
    cleanup:     \
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
    BEGIN_EXPR("a")
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, 'a'));
    EXPECT_EQ(fixture.parser.num_parsed, 1);
    EXPECT_EQ(fixture.parsed->num_groups, 0);
    END_EXPR;
}

static void test_concat_is_left_assoc(void)
{
    BEGIN_EXPR("abc");
    const cegex_parse_node* root = fixture.parsed->root;

    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_CONCAT) &&
                NODE_IS(LHS(root), CEGEX_PARSE_NODE_CONCAT) &&
                NODE_CHARCODE(LHS(LHS(root)), 'a') &&
                NODE_CHARCODE(RHS(LHS(root)), 'b') &&
                NODE_CHARCODE(RHS(root), 'c'));

    END_EXPR;
}

static void test_alternation_simple(void)
{
    BEGIN_EXPR("a|b");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_ALTERNATE) &&
                NODE_CHARCODE(LHS(root), 'a') &&
                NODE_CHARCODE(RHS(root), 'b'));

    END_EXPR;
}

static void test_alternation_chain_is_left_assoc(void)
{
    BEGIN_EXPR("a|b|c");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_ALTERNATE) &&
                NODE_IS(LHS(root), CEGEX_PARSE_NODE_ALTERNATE) &&
                NODE_CHARCODE(LHS(LHS(root)), 'a') &&
                NODE_CHARCODE(RHS(LHS(root)), 'b') &&
                NODE_CHARCODE(RHS(root), 'c'));
    END_EXPR;
}

static void test_concat_binds_tighter_than_alternation(void)
{
    BEGIN_EXPR("ab|cd");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_ALTERNATE) &&
                NODE_IS(LHS(root), CEGEX_PARSE_NODE_CONCAT) &&
                NODE_IS(RHS(root), CEGEX_PARSE_NODE_CONCAT) &&
                NODE_CHARCODE(LHS(LHS(root)), 'a') && NODE_CHARCODE(RHS(LHS(root)), 'b') &&
                NODE_CHARCODE(LHS(RHS(root)), 'c') && NODE_CHARCODE(RHS(RHS(root)), 'd'));
    END_EXPR;
}

static void test_alternation_rhs_concat(void)
{
    BEGIN_EXPR("a|bc");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_ALTERNATE) &&
                NODE_CHARCODE(LHS(root), 'a') &&
                NODE_IS(RHS(root), CEGEX_PARSE_NODE_CONCAT) &&
                NODE_CHARCODE(LHS(RHS(root)), 'b') &&
                NODE_CHARCODE(RHS(RHS(root)), 'c'));
    END_EXPR;
}

static void test_dot(void)
{
    BEGIN_EXPR(".");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_CHARSET) &&
                root->as.set.negated &&
                cegex_charset_contains(&root->as.set, '\n') &&
                cegex_charset_contains(&root->as.set, '\r'));
    END_EXPR;
}

static void test_digit_class(void)
{
    BEGIN_EXPR("\\d");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_CHARSET) &&
                !root->as.set.negated &&
                cegex_charset_contains(&root->as.set, '0') &&
                cegex_charset_contains(&root->as.set, '9') &&
                !cegex_charset_contains(&root->as.set, 'a'));
    END_EXPR;
}

static void test_word_class(void)
{
    BEGIN_EXPR("\\w");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_CHARSET) &&
                !root->as.set.negated &&
                cegex_charset_contains(&root->as.set, 'a') &&
                cegex_charset_contains(&root->as.set, '9') &&
                cegex_charset_contains(&root->as.set, '_') &&
                !cegex_charset_contains(&root->as.set, '-'));
    END_EXPR;
}

static void test_whitespace_class(void)
{
    BEGIN_EXPR("\\s");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_CHARSET) &&
                !root->as.set.negated &&
                cegex_charset_contains(&root->as.set, ' ') &&
                cegex_charset_contains(&root->as.set, '\t') &&
                !cegex_charset_contains(&root->as.set, 'a'));

    END_EXPR;
}

static void test_escaped_meta_literals(void)
{
    BEGIN_EXPR("\\.");
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '.'));

    EXPECT_EQ_GOTO(fixture_parse(&fixture, "\\*"), CEGEX_OK, cleanup);
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '*'));

    EXPECT_EQ_GOTO(fixture_parse(&fixture, "\\("), CEGEX_OK, cleanup);
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '('));

    EXPECT_EQ_GOTO(fixture_parse(&fixture, "\\\\"), CEGEX_OK, cleanup);
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '\\'));

    EXPECT_EQ_GOTO(fixture_parse(&fixture, "\\["), CEGEX_OK, cleanup);
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '['));

    END_EXPR;
}

static void test_escaped_ctrl(void)
{
    BEGIN_EXPR("\\n");
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '\n'));

    EXPECT_EQ_GOTO(fixture_parse(&fixture, "\\t"), CEGEX_OK, cleanup);
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '\t'));

    EXPECT_EQ_GOTO(fixture_parse(&fixture, "\\r"), CEGEX_OK, cleanup);
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '\r'));

    END_EXPR;
}

static void test_hex_escape(void)
{
    BEGIN_EXPR("\\x41");
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, 'A'));
    END_EXPR;
}

static void test_hex_escape_followed_by_charcode(void)
{
    BEGIN_EXPR("\\x41b");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_CONCAT) &&
                NODE_CHARCODE(LHS(root), 'A') &&
                NODE_CHARCODE(RHS(root), 'b'));
    END_EXPR;
}

static void test_octal_escape(void)
{
    BEGIN_EXPR("\\101");
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, 'A'));

    EXPECT_EQ_GOTO(fixture_parse(&fixture, "\\61"), CEGEX_OK, cleanup);
    EXPECT_TRUE(NODE_CHARCODE(fixture.parsed->root, '1'));

    EXPECT_EQ_GOTO(fixture_parse(&fixture, "\\61b"), CEGEX_OK, cleanup);

    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_CONCAT) &&
                NODE_CHARCODE(LHS(root), '1') &&
                NODE_CHARCODE(RHS(root), 'b'));

    END_EXPR;
}

static void test_incomplete_hex_escape_rejected(void)
{
    EXPECT_REJECTED("\\x4");
    EXPECT_REJECTED("\\x");
}

static void test_capturing_group(void)
{
    BEGIN_EXPR("(a)");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_GROUP) &&
                root->data.group.is_capturing &&
                NODE_CHARCODE(VAL(root), 'a'));

    EXPECT_TRUE(fixture.parsed->num_groups == 1);
    END_EXPR;
}

static void test_non_capturing_group(void)
{
    BEGIN_EXPR("(?:a)");
    const cegex_parse_node* root = fixture.parsed->root;
    EXPECT_TRUE(NODE_IS(root, CEGEX_PARSE_NODE_GROUP) && !root->data.group.is_capturing && NODE_CHARCODE(VAL(root), 'a'));
    EXPECT_TRUE(fixture.parsed->num_groups == 0);
    END_EXPR;
}

static void test_nested_group_numbering(void)
{
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
    END_EXPR;
}

static void test_non_capturing_groups_do_not_consume_indices(void)
{
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
    END_EXPR;
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

    return print_summary();
}
#include "../src/tokenizer.h"
#include "testing.h"

#define EXPECT_TOKEN(array, index, expected_type, expected_start, expected_length) \
    do {                                                                           \
        EXPECT_EQ((array).elements[index].type, expected_type);                    \
        EXPECT_EQ((array).elements[index].start, expected_start);                  \
        EXPECT_EQ((array).elements[index].length, expected_length);                \
    } while(0)

static void test_basic_regex(void)
{
    cegex_tokenizer ctx = { 0 };
    cegex_tokenized_expr* tokenized;

    const char* regex = "^abc.*$";
    EXPECT_EQ(cegex_tokenizer_exec(&ctx, regex, 7, &tokenized), CEGEX_OK);

    EXPECT_EQ(tokenized->tokens.length, 7);

    EXPECT_TOKEN(tokenized->tokens, 0, CEGEX_TOKEN_HAT, 0, 1);
    EXPECT_TOKEN(tokenized->tokens, 1, CEGEX_TOKEN_CHARCODE, 1, 1);
    EXPECT_TOKEN(tokenized->tokens, 2, CEGEX_TOKEN_CHARCODE, 2, 1);
    EXPECT_TOKEN(tokenized->tokens, 3, CEGEX_TOKEN_CHARCODE, 3, 1);
    EXPECT_TOKEN(tokenized->tokens, 4, CEGEX_TOKEN_CLASS_DOT, 4, 1);
    EXPECT_TOKEN(tokenized->tokens, 5, CEGEX_TOKEN_STAR, 5, 1);
    EXPECT_TOKEN(tokenized->tokens, 6, CEGEX_TOKEN_DOLLAR, 6, 1);

    cegex_tokenizer_free(&ctx);
}

static void test_character_classes(void)
{
    cegex_tokenizer ctx = { 0 };
    cegex_tokenized_expr* tokenized;

    const char* regex = "\\d\\D\\w\\W\\s\\S";
    EXPECT_EQ(cegex_tokenizer_exec(&ctx, regex, 12, &tokenized), CEGEX_OK);

    EXPECT_EQ(tokenized->tokens.length, 6);

    EXPECT_TOKEN(tokenized->tokens, 0, CEGEX_TOKEN_CLASS_DIGIT, 0, 2);
    EXPECT_TOKEN(tokenized->tokens, 1, CEGEX_TOKEN_CLASS_NOT_DIGIT, 2, 2);
    EXPECT_TOKEN(tokenized->tokens, 2, CEGEX_TOKEN_CLASS_WORD, 4, 2);
    EXPECT_TOKEN(tokenized->tokens, 3, CEGEX_TOKEN_CLASS_NOT_WORD, 6, 2);
    EXPECT_TOKEN(tokenized->tokens, 4, CEGEX_TOKEN_CLASS_WHITESPACE, 8, 2);
    EXPECT_TOKEN(tokenized->tokens, 5, CEGEX_TOKEN_CLASS_NOT_WHITESPACE, 10, 2);

    cegex_tokenizer_free(&ctx);
}

static void test_groups_alternation_and_quantifiers(void)
{
    cegex_tokenizer ctx = { 0 };
    cegex_tokenized_expr* tokenized;

    const char* regex = "(rat|bat)+?";
    EXPECT_EQ(cegex_tokenizer_exec(&ctx, regex, 11, &tokenized), CEGEX_OK);

    EXPECT_EQ(tokenized->tokens.length, 11);

    EXPECT_TOKEN(tokenized->tokens, 0, CEGEX_TOKEN_PAREN_OPEN, 0, 1);
    EXPECT_TOKEN(tokenized->tokens, 1, CEGEX_TOKEN_CHARCODE, 1, 1);
    EXPECT_TOKEN(tokenized->tokens, 2, CEGEX_TOKEN_CHARCODE, 2, 1);
    EXPECT_TOKEN(tokenized->tokens, 3, CEGEX_TOKEN_CHARCODE, 3, 1);
    EXPECT_TOKEN(tokenized->tokens, 4, CEGEX_TOKEN_ALTERNATE, 4, 1);
    EXPECT_TOKEN(tokenized->tokens, 5, CEGEX_TOKEN_CHARCODE, 5, 1);
    EXPECT_TOKEN(tokenized->tokens, 6, CEGEX_TOKEN_CHARCODE, 6, 1);
    EXPECT_TOKEN(tokenized->tokens, 7, CEGEX_TOKEN_CHARCODE, 7, 1);
    EXPECT_TOKEN(tokenized->tokens, 8, CEGEX_TOKEN_PAREN_CLOSE, 8, 1);
    EXPECT_TOKEN(tokenized->tokens, 9, CEGEX_TOKEN_PLUS, 9, 1);
    EXPECT_TOKEN(tokenized->tokens, 10, CEGEX_TOKEN_QUESTION_MARK, 10, 1);

    cegex_tokenizer_free(&ctx);
}

static void test_bracket_expression(void)
{
    cegex_tokenizer ctx = { 0 };
    cegex_tokenized_expr* tokenized;

    const char* regex = "[a-zA-Z0-9]";
    EXPECT_EQ(cegex_tokenizer_exec(&ctx, regex, 11, &tokenized), CEGEX_OK);

    EXPECT_EQ(tokenized->tokens.length, 11);

    EXPECT_TOKEN(tokenized->tokens, 0, CEGEX_TOKEN_SQUARE_OPEN, 0, 1);
    EXPECT_TOKEN(tokenized->tokens, 1, CEGEX_TOKEN_CHARCODE, 1, 1);
    EXPECT_TOKEN(tokenized->tokens, 2, CEGEX_TOKEN_MINUS, 2, 1);
    EXPECT_TOKEN(tokenized->tokens, 3, CEGEX_TOKEN_CHARCODE, 3, 1);
    EXPECT_TOKEN(tokenized->tokens, 4, CEGEX_TOKEN_CHARCODE, 4, 1);
    EXPECT_TOKEN(tokenized->tokens, 5, CEGEX_TOKEN_MINUS, 5, 1);
    EXPECT_TOKEN(tokenized->tokens, 6, CEGEX_TOKEN_CHARCODE, 6, 1);
    EXPECT_TOKEN(tokenized->tokens, 7, CEGEX_TOKEN_CHARCODE, 7, 1);
    EXPECT_TOKEN(tokenized->tokens, 8, CEGEX_TOKEN_MINUS, 8, 1);
    EXPECT_TOKEN(tokenized->tokens, 9, CEGEX_TOKEN_CHARCODE, 9, 1);
    EXPECT_TOKEN(tokenized->tokens, 10, CEGEX_TOKEN_SQUARE_CLOSE, 10, 1);

    cegex_tokenizer_free(&ctx);
}

static void test_escape_sequences(void)
{
    cegex_tokenizer ctx = { 0 };
    cegex_tokenized_expr* tokenized;

    const char* regex = "\\n\\t\\x41\\123";
    EXPECT_EQ(cegex_tokenizer_exec(&ctx, regex, 12, &tokenized), CEGEX_OK);

    EXPECT_EQ(tokenized->tokens.length, 9);

    EXPECT_TOKEN(tokenized->tokens, 0, CEGEX_TOKEN_ESCAPE_NEWLINE, 0, 2);
    EXPECT_TOKEN(tokenized->tokens, 1, CEGEX_TOKEN_ESCAPE_HORIZONTAL_TAB, 2, 2);

    EXPECT_TOKEN(tokenized->tokens, 2, CEGEX_TOKEN_ESCAPE_HEX, 4, 2);
    EXPECT_TOKEN(tokenized->tokens, 3, CEGEX_TOKEN_CHARCODE, 6, 1);
    EXPECT_TOKEN(tokenized->tokens, 4, CEGEX_TOKEN_CHARCODE, 7, 1);

    EXPECT_TOKEN(tokenized->tokens, 5, CEGEX_TOKEN_ESCAPE_OCTAL, 8, 1);
    EXPECT_TOKEN(tokenized->tokens, 6, CEGEX_TOKEN_CHARCODE, 9, 1);
    EXPECT_TOKEN(tokenized->tokens, 7, CEGEX_TOKEN_CHARCODE, 10, 1);
    EXPECT_TOKEN(tokenized->tokens, 8, CEGEX_TOKEN_CHARCODE, 11, 1);

    cegex_tokenizer_free(&ctx);
}

static void test_dangling_backslash(void)
{
    cegex_tokenizer ctx = { 0 };
    cegex_tokenized_expr* tokenized;

    const char* regex = "abc\\";
    EXPECT_EQ(cegex_tokenizer_exec(&ctx, regex, 4, &tokenized), CEGEX_ERR_SYNTAX);

    cegex_tokenizer_free(&ctx);
}

int main(void)
{
    RUN_TEST(test_basic_regex);
    RUN_TEST(test_character_classes);
    RUN_TEST(test_groups_alternation_and_quantifiers);
    RUN_TEST(test_bracket_expression);
    RUN_TEST(test_escape_sequences);
    RUN_TEST(test_dangling_backslash);

    return print_summary();
}
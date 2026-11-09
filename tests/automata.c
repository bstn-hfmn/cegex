#include "../src/automata.h"
#include "../src/common.h"
#include "testing.h"
#include <stdbool.h>

typedef struct nfa_test_case
{
    const char* subject;
    size_t length;
    bool expected;
} nfa_test_case;

static const nfa_test_case test_cases[] = {
    { "c", 1, true },
    { "ac", 2, true },
    { "aac", 3, true },
    { "aaac", 4, true },
    { "abdc", 4, true },
    { "abec", 4, true },
    { "abdabdc", 7, true },
    { "abdabec", 7, true },
    { "aabdc", 5, true },
    { "abdaac", 6, true },
    { "aabdaac", 7, true },
    { "abdaabec", 8, true },
    { "aabdabdaac", 10, true },

    { "", 0, false },
    { "a", 1, false },
    { "ab", 2, false },
    { "abd", 3, false },
    { "abc", 3, false },
    { "abfc", 4, false },
    { "abcc", 4, false },
    { "abac", 4, false },
    { "bac", 3, false },
    { "dac", 3, false },
    { "acb", 3, false },
    { "acc", 3, false },
    { "aca", 3, false },
    { "abcd", 4, false },
    { "abdcx", 5, false },
    { "cc", 2, false },
    { "cac", 3, false },
    { "abecabdc", 8, false },
};

static void test_equality_helpers(void)
{
    nfa_state_id a = 7, b = 7, c = 8;
    nfa_match_entry x = { .subject_index = 2, .state_id = 7 };
    nfa_match_entry y = { .subject_index = 2, .state_id = 7 };
    nfa_match_entry z = { .subject_index = 3, .state_id = 7 };
    byte p = 'q', q = 'q', r = 'r';

    EXPECT_TRUE(nfa_state_id_eq(&a, &b));
    EXPECT_TRUE(!nfa_state_id_eq(&a, &c));
    EXPECT_TRUE(nfa_match_entry_eq(&x, &y));
    EXPECT_TRUE(!nfa_match_entry_eq(&x, &z));
    EXPECT_TRUE(single_byte_eq(&p, &q));
    EXPECT_TRUE(!single_byte_eq(&p, &r));
}

static void test_init_and_state_creation(void)
{
    nfa ctx;
    nfa_state_id first = (nfa_state_id) -1;
    nfa_state_id second = (nfa_state_id) -1;

    EXPECT_EQ_GOTO(nfa_init(&ctx), CEGEX_OK, fail);

    EXPECT_EQ(nfa_create_state(&ctx, NFA_STATE_INITIAL, &first), CEGEX_OK);
    EXPECT_EQ(nfa_create_state(&ctx, NFA_STATE_FINAL, &second), CEGEX_OK);

    EXPECT_TRUE(first != second);
    EXPECT_TRUE(ctx.states.elements != NULL);
    EXPECT_EQ(ctx.states.length, 2);

fail:
    nfa_free(&ctx);
}

static void test_create_transition_and_validation(void)
{
    nfa ctx;
    nfa_state_id from, to1, to2, unused;
    nfa_transition* transition = NULL;

    EXPECT_EQ_GOTO(nfa_init(&ctx), CEGEX_OK, fail);

    EXPECT_EQ_GOTO(nfa_create_state(&ctx, NFA_STATE_DEFAULT, &from), CEGEX_OK, fail);
    EXPECT_EQ_GOTO(nfa_create_state(&ctx, NFA_STATE_DEFAULT, &to1), CEGEX_OK, fail);
    EXPECT_EQ_GOTO(nfa_create_state(&ctx, NFA_STATE_DEFAULT, &to2), CEGEX_OK, fail);
    EXPECT_EQ_GOTO(nfa_create_state(&ctx, NFA_STATE_DEFAULT, &unused), CEGEX_OK, fail);

    EXPECT_EQ(nfa_create_transition(
                  &ctx, from, to1, NFA_TRANSITION_EPSILON, &transition),
              CEGEX_OK);

    EXPECT_NOT_NULL(transition);
    if(transition != NULL) {
        EXPECT_EQ(transition->from, from);
        EXPECT_EQ(transition->to, to1);
        EXPECT_EQ(transition->condition.type, NFA_TRANSITION_COND_EPSILON);
    }

    transition = NULL;
    EXPECT_EQ(nfa_create_transition(
                  &ctx, from, to2, NFA_TRANSITION_EPSILON, &transition),
              CEGEX_OK);
    EXPECT_NOT_NULL(transition);

    EXPECT_EQ(nfa_create_transition(
                  &ctx, from, unused, NFA_TRANSITION_EPSILON, NULL),
              CEGEX_ERR_INVALID_ARGUMENT);

    EXPECT_EQ(nfa_create_transition(
                  &ctx, (nfa_state_id) -1, to1,
                  NFA_TRANSITION_EPSILON, NULL),
              CEGEX_ERR_INVALID_ARGUMENT);

    EXPECT_EQ(nfa_create_transition(
                  &ctx, unused, (nfa_state_id) -1,
                  NFA_TRANSITION_EPSILON, NULL),
              CEGEX_ERR_INVALID_ARGUMENT);

fail:
    nfa_free(&ctx);
}

static void test_nfa_cases(void)
{
    nfa ctx;
    EXPECT_EQ_GOTO(nfa_init(&ctx), CEGEX_OK, fail);
    nfa_fragment abde, de, a, a2, b, c, ab, unioned, repeated, root;

    EXPECT_EQ_GOTO(NFA_CREATE_SYMBOLS(&ctx, ((byte[]){ 'd', 'e' }), &de), CEGEX_OK, fail);
    EXPECT_EQ_GOTO(NFA_CREATE_SYMBOL(&ctx, 'a', &a), CEGEX_OK, fail);
    EXPECT_EQ_GOTO(NFA_CREATE_SYMBOL(&ctx, 'a', &a2), CEGEX_OK, fail);
    EXPECT_EQ_GOTO(NFA_CREATE_SYMBOL(&ctx, 'b', &b), CEGEX_OK, fail);
    EXPECT_EQ_GOTO(NFA_CREATE_SYMBOL(&ctx, 'c', &c), CEGEX_OK, fail);

    EXPECT_EQ_GOTO(nfa_concat(&ctx, a, b, &ab), CEGEX_OK, fail);          // ab
    EXPECT_EQ_GOTO(nfa_concat(&ctx, ab, de, &abde), CEGEX_OK, fail);      // ab[de]
    EXPECT_EQ_GOTO(nfa_union(&ctx, abde, a2, &unioned), CEGEX_OK, fail);  // ab[de]|a
    EXPECT_EQ_GOTO(nfa_star(&ctx, unioned, &repeated), CEGEX_OK, fail);   // (ab[de]|a)*
    EXPECT_EQ_GOTO(nfa_concat(&ctx, repeated, c, &root), CEGEX_OK, fail); // (ab[de]|a)*c

    for(size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); ++i) {
        const nfa_test_case* test = &test_cases[i];
        bool actual = nfa_matches(
            &ctx,
            root,
            test->subject,
            test->length);

        EXPECT_EQ(actual, test->expected);
    }

fail:
    nfa_free(&ctx);
}

int main(void)
{
    RUN_TEST(test_equality_helpers);
    RUN_TEST(test_init_and_state_creation);
    RUN_TEST(test_create_transition_and_validation);
    RUN_TEST(test_nfa_cases);
    return print_summary();
}
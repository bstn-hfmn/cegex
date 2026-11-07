#include "../util/collections.h"
#include "testing.h"

typedef struct test_struct
{
    size_t index;
    bool boolean;

    struct
    {
        char arr[32];
    } anon;
} test_struct;

typedef ARRAY(test_struct) test_array;

static void test_array_init(void)
{

    test_array arr;

    ARRAY_INIT(&arr);
    EXPECT_EQ(arr.capacity, 53);
    EXPECT_EQ(arr.length, 0);
    EXPECT_NOT_NULL(arr.elements);

    ARRAY_FREE(&arr);
    EXPECT_NULL(arr.elements);
    EXPECT_EQ(arr.capacity, 0);
    EXPECT_EQ(arr.length, 0);
}

static void test_array_push(void)
{
    test_array arr;
    ARRAY_INIT(&arr);
    EXPECT_EQ(arr.length, 0);

    for(size_t i = 0; i < 5; i++) {
        test_struct entry = { 0 };
        ARRAY_PUSH(&arr, entry);
    }

    EXPECT_EQ(arr.length, 5);
    ARRAY_FREE(&arr);
}

static void test_array_pop(void)
{
    test_array arr;
    ARRAY_INIT(&arr);
    EXPECT_EQ(arr.length, 0);

    for(size_t i = 0; i < 5; i++) {
        test_struct entry = { .index = i };
        ARRAY_PUSH(&arr, entry);
    }

    EXPECT_EQ(arr.length, 5);

    test_struct popped;
    ARRAY_POP(&arr, &popped);
    EXPECT_EQ(arr.length, 4);
    EXPECT_EQ(popped.index, 4);

    ARRAY_POP(&arr, &popped);
    EXPECT_EQ(arr.length, 3);
    EXPECT_EQ(popped.index, 3);

    ARRAY_FREE(&arr);
}

static void test_array_nth(void)
{
    test_array arr;
    ARRAY_INIT(&arr);
    EXPECT_EQ(arr.length, 0);

    for(size_t i = 0; i < 5; i++) {
        test_struct entry = { .index = i };
        ARRAY_PUSH(&arr, entry);
    }
    EXPECT_EQ(arr.length, 5);

    test_struct out = { 0 };
    ARRAY_NTH(&arr, 0, &out);
    EXPECT_EQ(out.index, 0);

    ARRAY_NTH(&arr, 1, &out);
    EXPECT_EQ(out.index, 1);

    ARRAY_NTH(&arr, 2, &out);
    EXPECT_EQ(out.index, 2);

    test_struct* pout = NULL;
    ARRAY_NTH_REF(&arr, 0, &pout);
    EXPECT_EQ((test_struct*) &arr.elements[0], pout);
    EXPECT_EQ(pout->index, 0);

    ARRAY_NTH_REF(&arr, 1, &pout);
    EXPECT_EQ((test_struct*) &arr.elements[1], pout);
    EXPECT_EQ(pout->index, 1);

    ARRAY_NTH_REF(&arr, 2, &pout);
    EXPECT_EQ((test_struct*) &arr.elements[2], pout);
    EXPECT_EQ(pout->index, 2);

    ARRAY_FREE(&arr);
}

static void test_capacity_grow(void)
{
    size_t next_capacity;
    next_prime_capacity(0, &next_capacity);

    EXPECT_EQ(next_capacity, 53);
    next_prime_capacity(next_capacity + 1, &next_capacity);
    EXPECT_EQ(next_capacity, 97);
    next_prime_capacity(next_capacity + 1, &next_capacity);
    EXPECT_EQ(next_capacity, 193);

    test_array arr;
    ARRAY_INIT(&arr);
    for(size_t i = 0; i < 100; i++) {
        test_struct entry = { 0 };
        ARRAY_PUSH(&arr, entry);
    }

    EXPECT_EQ(arr.capacity, 193);
    EXPECT_EQ(arr.length, 100);
    ARRAY_FREE(&arr);
}

int main(void)
{
    RUN_TEST(test_array_init);
    RUN_TEST(test_array_push);
    RUN_TEST(test_array_pop);
    RUN_TEST(test_array_nth);
    RUN_TEST(test_capacity_grow);

    return print_summary();
}
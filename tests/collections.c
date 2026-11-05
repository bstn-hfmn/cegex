#include "../util/collections.h"
#include "testing.h"

static void test_capacity_grow(void)
{
    size_t next_capacity;
    next_prime_capacity(0, &next_capacity);

    EXPECT_EQ(next_capacity, 53);
    next_prime_capacity(next_capacity + 1, &next_capacity);
    EXPECT_EQ(next_capacity, 97);
}

int main(void)
{
    RUN_TEST(test_capacity_grow);

    return print_summary();
}
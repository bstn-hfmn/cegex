#ifndef CEGEX_TEST_FRAMEWORK_H
#define CEGEX_TEST_FRAMEWORK_H

#include <stdio.h>

static struct
{
    size_t run;
    size_t passed;
    size_t failed;
} _testing_stats = { 0, 0, 0 };

#define REPORT_FAILURE(...)                               \
    do {                                                  \
        fprintf(stderr, "[%s:%d]: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__);                     \
        fputc('\n', stderr);                              \
        _testing_stats.failed++;                          \
    } while(0)

#define EXPECT_TRUE(cond)                               \
    do {                                                \
        if(!(cond))                                     \
            REPORT_FAILURE("expected true: %s", #cond); \
    } while(0)

#define EXPECT_EQ(actual, expected)             \
    do {                                        \
        if(actual != expected) {                \
            REPORT_FAILURE("%s == %s failed",   \
                           #actual, #expected); \
        }                                       \
    } while(0)

#define EXPECT_EQ_GOTO(actual, expected, go)    \
    do {                                        \
        if(actual != expected) {                \
            REPORT_FAILURE("%s == %s failed",   \
                           #actual, #expected); \
            goto go;                            \
        }                                       \
    } while(0)

#define EXPECT_NOT_NULL(pointer)                                       \
    do {                                                               \
        if(pointer == NULL) {                                          \
            REPORT_FAILURE("%s is null, expected not null", #pointer); \
        }                                                              \
    } while(0)

#define EXPECT_NULL(pointer)                                           \
    do {                                                               \
        if(pointer != NULL) {                                          \
            REPORT_FAILURE("%s is not null, expected null", #pointer); \
        }                                                              \
    } while(0)

#define RUN_TEST(fn)                           \
    do {                                       \
        size_t failed = _testing_stats.failed; \
        _testing_stats.run++;                  \
        fn();                                  \
        if(_testing_stats.failed > failed) {   \
            printf("[FAIL] %s\n", #fn);        \
        } else {                               \
            _testing_stats.passed++;           \
            printf("[ OK ] %s\n", #fn);        \
        }                                      \
    } while(0)

static int print_summary(void)
{
    printf("\n%zu run, %zu passed, %zu failed\n",
           _testing_stats.run, _testing_stats.passed, _testing_stats.failed);

    return _testing_stats.failed == 0 ? 0 : 1;
}

#endif
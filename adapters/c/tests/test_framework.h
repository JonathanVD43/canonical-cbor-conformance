#ifndef ADAPTER_TEST_FRAMEWORK_H
#define ADAPTER_TEST_FRAMEWORK_H

/* Minimal hand-rolled assert()-based test framework -- C has no built-in
 * test runner, and per CONTRIBUTING.md's stdlib/hand-rolled-first
 * convention a third-party framework (Unity, CMocka, ...) was ruled out
 * for something this small. Each test function calls CHECK(...) for every
 * assertion; a failing CHECK prints a diagnostic and marks the run failed
 * but does NOT abort the process, so one test function reports every
 * failure it hits rather than stopping at the first. */

#include <stdio.h>

extern int g_test_total;
extern int g_test_fail;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        g_test_total++;                                                             \
        if (!(cond)) {                                                              \
            g_test_fail++;                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);       \
        }                                                                            \
    } while (0)

#define CHECK_STREQ(a, b)                                                           \
    do {                                                                            \
        g_test_total++;                                                             \
        const char *_a = (a);                                                       \
        const char *_b = (b);                                                       \
        if (_a == NULL || _b == NULL || strcmp(_a, _b) != 0) {                      \
            g_test_fail++;                                                          \
            fprintf(stderr, "FAIL: %s == %s -> got \"%s\" want \"%s\" (%s:%d)\n",   \
                    #a, #b, _a ? _a : "(null)", _b ? _b : "(null)", __FILE__, __LINE__); \
        }                                                                            \
    } while (0)

#endif

/* SPDX-License-Identifier: GPL-2.0 */
/* A minimal test harness: CHECK records failures, TEST_MAIN reports them. */
#ifndef NTPROF_TESTLIB_H
#define NTPROF_TESTLIB_H

#include <stdio.h>
#include <stdlib.h>

static int test_failures;
static int test_checks;

#define CHECK(cond)                                                      \
  do {                                                                   \
    test_checks++;                                                       \
    if (!(cond)) {                                                       \
      test_failures++;                                                   \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
              #cond);                                                    \
    }                                                                    \
  } while (0)

#define CHECK_EQ(a, b)                                                   \
  do {                                                                   \
    long long _a = (long long)(a), _b = (long long)(b);                  \
    test_checks++;                                                       \
    if (_a != _b) {                                                      \
      test_failures++;                                                   \
      fprintf(stderr, "%s:%d: %s == %lld, expected %s == %lld\n",        \
              __FILE__, __LINE__, #a, _a, #b, _b);                       \
    }                                                                    \
  } while (0)

#define RUN(fn)                                                          \
  do {                                                                   \
    int _before = test_failures;                                         \
    fn();                                                                \
    printf("  %-40s %s\n", #fn, test_failures == _before ? "ok" : "FAIL"); \
  } while (0)

#define TEST_DONE()                                                      \
  do {                                                                   \
    printf("%d checks, %d failed\n", test_checks, test_failures);        \
    return test_failures ? EXIT_FAILURE : EXIT_SUCCESS;                  \
  } while (0)

#endif /* NTPROF_TESTLIB_H */

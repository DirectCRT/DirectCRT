/* The tests' checks: each counts, and a failed one prints where it is */

#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>
#include <string.h>

static int g_failed;
static int g_checks;

#define CHECK(cond) \
    do { \
        g_checks++; \
        if (!(cond)) { g_failed++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

#define CHECK_STR(a, b) \
    do { \
        const char* a_ = (a); const char* b_ = (b); \
        g_checks++; \
        if ((a_ == NULL) != (b_ == NULL) || (a_ && strcmp(a_, b_) != 0)) { \
            g_failed++; \
            printf("  FAIL %s:%d: %s is \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, \
                a_ ? a_ : "(null)", b_ ? b_ : "(null)"); \
        } \
    } while (0)

/* The summary line; the exit code */
static int check_report(const char* name)
{
    printf("%s: %d checks, %d failed\n", name, g_checks, g_failed);
    return g_failed ? 1 : 0;
}

#endif

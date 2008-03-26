#include "test.h"
#include "lang.h"

typedef void *(*alloc_ft)(size_t);

static void huge_emalloc(void *data)
{
    (void)data;
    frt_malloc((size_t)-1);
}

static void huge_ecalloc(void *data)
{
    (void)data;
    frt_calloc((size_t)-1);
}

static void huge_erealloc(void *data)
{
    char * p = NULL;
    (void)data;
    frt_realloc(p, (size_t)-1);
}

static void test_emalloc(tst_case *tc, void *data)
{
    char *p;
    (void)data; /* suppress warning */

    p = frt_malloc(100);
    Apnotnull(p);
    free(p);

    Araise(MEM_ERROR, huge_emalloc, NULL);
}

static void test_ecalloc(tst_case *tc, void *data)
{
    int i;
    char *p;
    (void)data; /* suppress warning */

    p = frt_calloc(100);
    Apnotnull(p);
    for (i = 0; i < 100; ++i) {
        Aiequal(p[i], 0);
    }
    free(p);

    Araise(MEM_ERROR, huge_ecalloc, NULL);
}

static void test_erealloc(tst_case *tc, void *data)
{
    char *p = NULL;
    (void)data; /* suppress warning */

    p = frt_realloc(p, 100);
    Apnotnull(p);
    free(p);

    Araise(MEM_ERROR, huge_erealloc, NULL);
}

tst_suite *ts_lang(tst_suite *suite)
{
    suite = ADD_SUITE(suite);

    tst_run_test(suite, test_emalloc, NULL);
    tst_run_test(suite, test_ecalloc, NULL);
    tst_run_test(suite, test_erealloc, NULL);

    return suite;
}
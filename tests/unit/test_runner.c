/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * test_runner.c - unit tests for nxe_phase_sort_entries()
 *
 * nxe_phase_sort_entries() is static, so it can only be reached by
 * pulling the whole translation unit into this file via #include
 * rather than linking against a separately compiled object -- see
 * ngx_compat/ngx_http.h for the stub surface that makes ../../src/
 * nxe_phase.c compile standalone.
 */

#include "../../src/nxe_phase.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int tests_run = 0;
static int tests_failed = 0;
static int current_test_failed = 0;
static int verbose = 0;


#define TEST(name)  static void test_ ## name(void)

#define RUN(name)                                                          \
        do {                                                                   \
            current_test_failed = 0;                                          \
            tests_run++;                                                      \
            test_ ## name();                                                    \
            if (current_test_failed) {                                        \
                tests_failed++;                                               \
                printf("FAIL  %s\n", #name);                                  \
            } else if (verbose) {                                             \
                printf("PASS  %s\n", #name);                                  \
            }                                                                  \
        } while (0)

#define ASSERT(cond)                                                       \
        do {                                                                   \
            if (!(cond)) {                                                    \
                printf("  assertion failed: %s (%s:%d)\n", #cond, __FILE__,   \
                       __LINE__);                                                \
                current_test_failed = 1;                                      \
            }                                                                  \
        } while (0)

#define ASSERT_EQ_INT(a, b)                                                \
        do {                                                                   \
            long long a_ = (long long) (a);                                  \
            long long b_ = (long long) (b);                                  \
            if (a_ != b_) {                                                   \
                printf("  assertion failed: %s (%lld) != %s (%lld) "          \
                       "(%s:%d)\n", #a, a_, #b, b_, __FILE__, __LINE__);         \
                current_test_failed = 1;                                      \
            }                                                                  \
        } while (0)

#define ASSERT_STR_EQ(a, b)                                                \
        do {                                                                   \
            const char *a_ = (a);                                            \
            const char *b_ = (b);                                            \
            if (strcmp(a_, b_) != 0) {                                        \
                printf("  assertion failed: %s (\"%s\") != %s (\"%s\") "      \
                       "(%s:%d)\n", #a, a_, #b, b_, __FILE__, __LINE__);         \
                current_test_failed = 1;                                      \
            }                                                                  \
        } while (0)


static nxe_phase_entry_t
mkentry(const char *name, ngx_int_t prio, ngx_uint_t seq)
{
    nxe_phase_entry_t e;

    e.handler = NULL;
    e.prio = prio;
    e.seq = seq;
    e.name = name;

    return e;
}


TEST(ascending_priority_sorts_descending_by_prio){
    nxe_phase_entry_t entries[3];

    entries[0] = mkentry("c", 300, 0);
    entries[1] = mkentry("a", 100, 1);
    entries[2] = mkentry("b", 200, 2);

    ASSERT_EQ_INT(nxe_phase_sort_entries(entries, 3), NGX_OK);

    ASSERT_STR_EQ(entries[0].name, "c");
    ASSERT_STR_EQ(entries[1].name, "b");
    ASSERT_STR_EQ(entries[2].name, "a");
}


TEST(same_priority_sorts_descending_by_seq){
    nxe_phase_entry_t entries[3];

    entries[0] = mkentry("first", 100, 0);
    entries[1] = mkentry("second", 100, 1);
    entries[2] = mkentry("third", 100, 2);

    ASSERT_EQ_INT(nxe_phase_sort_entries(entries, 3), NGX_OK);

    /*
     * Descending seq puts the earliest-registered entry ("first", the
     * smallest seq) last -- the highest array index -- which is where
     * ngx_http_init_phase_handlers()'s tail-to-head walk runs first.
     * See nxe_phase_sort_entries()'s derivation comment in
     * src/nxe_phase.c.
     */
    ASSERT_STR_EQ(entries[0].name, "third");
    ASSERT_STR_EQ(entries[1].name, "second");
    ASSERT_STR_EQ(entries[2].name, "first");
}


TEST(mixed_priority_and_ties){
    nxe_phase_entry_t entries[5];

    entries[0] = mkentry("jwt", 200, 0);
    entries[1] = mkentry("apikey_first", 300, 1);
    entries[2] = mkentry("httpsig", 150, 2);
    entries[3] = mkentry("apikey_second", 300, 3);
    entries[4] = mkentry("oidc", 500, 4);

    ASSERT_EQ_INT(nxe_phase_sort_entries(entries, 5), NGX_OK);

    ASSERT_STR_EQ(entries[0].name, "oidc");
    ASSERT_STR_EQ(entries[1].name, "apikey_second");
    ASSERT_STR_EQ(entries[2].name, "apikey_first");
    ASSERT_STR_EQ(entries[3].name, "jwt");
    ASSERT_STR_EQ(entries[4].name, "httpsig");
}


TEST(does_not_touch_memory_outside_the_given_range){
    nxe_phase_entry_t buf[5];

    buf[0] = mkentry("sentinel_before", -1, 0);
    buf[1] = mkentry("c", 300, 0);
    buf[2] = mkentry("a", 100, 1);
    buf[3] = mkentry("b", 200, 2);
    buf[4] = mkentry("sentinel_after", -1, 0);

    ASSERT_EQ_INT(nxe_phase_sort_entries(&buf[1], 3), NGX_OK);

    ASSERT_STR_EQ(buf[0].name, "sentinel_before");
    ASSERT_STR_EQ(buf[4].name, "sentinel_after");

    ASSERT_STR_EQ(buf[1].name, "c");
    ASSERT_STR_EQ(buf[2].name, "b");
    ASSERT_STR_EQ(buf[3].name, "a");
}


TEST(result_independent_of_input_order){
    nxe_phase_entry_t order_a[4];
    nxe_phase_entry_t order_b[4];

    order_a[0] = mkentry("jwt", 200, 0);
    order_a[1] = mkentry("httpsig", 150, 1);
    order_a[2] = mkentry("apikey", 300, 2);
    order_a[3] = mkentry("oidc", 500, 3);

    order_b[0] = mkentry("oidc", 500, 3);
    order_b[1] = mkentry("apikey", 300, 2);
    order_b[2] = mkentry("jwt", 200, 0);
    order_b[3] = mkentry("httpsig", 150, 1);

    ASSERT_EQ_INT(nxe_phase_sort_entries(order_a, 4), NGX_OK);
    ASSERT_EQ_INT(nxe_phase_sort_entries(order_b, 4), NGX_OK);

    ASSERT_STR_EQ(order_a[0].name, order_b[0].name);
    ASSERT_STR_EQ(order_a[1].name, order_b[1].name);
    ASSERT_STR_EQ(order_a[2].name, order_b[2].name);
    ASSERT_STR_EQ(order_a[3].name, order_b[3].name);
}


TEST(empty_array){
    ASSERT_EQ_INT(nxe_phase_sort_entries(NULL, 0), NGX_OK);
}


TEST(single_element){
    nxe_phase_entry_t entries[1];

    entries[0] = mkentry("only", 200, 0);

    ASSERT_EQ_INT(nxe_phase_sort_entries(entries, 1), NGX_OK);

    ASSERT_STR_EQ(entries[0].name, "only");
    ASSERT_EQ_INT(entries[0].prio, 200);
}


TEST(all_equal_priority_preserves_seq_derived_order){
    nxe_phase_entry_t entries[4];

    entries[0] = mkentry("w", 100, 0);
    entries[1] = mkentry("x", 100, 1);
    entries[2] = mkentry("y", 100, 2);
    entries[3] = mkentry("z", 100, 3);

    ASSERT_EQ_INT(nxe_phase_sort_entries(entries, 4), NGX_OK);

    ASSERT_STR_EQ(entries[0].name, "z");
    ASSERT_STR_EQ(entries[1].name, "y");
    ASSERT_STR_EQ(entries[2].name, "x");
    ASSERT_STR_EQ(entries[3].name, "w");
}


int
main(void)
{
    if (getenv("NXE_PHASE_TEST_VERBOSE") != NULL) {
        verbose = 1;
    }

    RUN(ascending_priority_sorts_descending_by_prio);
    RUN(same_priority_sorts_descending_by_seq);
    RUN(mixed_priority_and_ties);
    RUN(does_not_touch_memory_outside_the_given_range);
    RUN(result_independent_of_input_order);
    RUN(empty_array);
    RUN(single_element);
    RUN(all_equal_priority_preserves_seq_derived_order);

    printf("%d/%d tests passed\n", tests_run - tests_failed, tests_run);

    return tests_failed == 0 ? 0 : 1;
}

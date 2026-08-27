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


/* --- test harness for nxe_phase_add_handler() phase whitelist tests --- */

#define NXE_PHASE_STR_(x)  #x
#define NXE_PHASE_STR(x)   NXE_PHASE_STR_(x)

/*
 * The registry (nxe_phase_registry, file-scope in nxe_phase.c) keys its
 * reset-detection on "registry.cycle != cf->cycle" pointer identity. A
 * stack-allocated ngx_cycle_t would have its address reused across
 * TEST() calls, so the registry would see the same pointer, skip the
 * reset, and leak phase-group slots from a prior test into the next
 * one. A distinct static array element per test avoids that.
 */
static ngx_cycle_t test_cycles[16];
static ngx_uint_t test_cycles_used = 0;

static ngx_module_t *modules_with_registry[] = {
    &NXE_PHASE_MODULE_SYM,
    NULL
};

static ngx_module_t *modules_without_registry[] = {
    NULL
};

static ngx_int_t
dummy_handler(ngx_http_request_t *r)
{
    (void) r;
    return NGX_OK;
}


/*
 * Builds an ngx_conf_t wired to a fresh cycle/pool/cmcf, with only the 8
 * phases real nginx initializes (ngx_http_init_phases(), nginx
 * src/http/ngx_http.c) given an initialized handlers array --
 * FIND_CONFIG / POST_REWRITE / POST_ACCESS are left zeroed, matching
 * production, so a test that mistakenly pushes into one of those fails
 * the same way production would instead of silently succeeding.
 */
static ngx_conf_t
make_test_conf(ngx_module_t **modules)
{
    ngx_conf_t cf;
    ngx_cycle_t *cycle;
    ngx_pool_t *pool;
    ngx_http_core_main_conf_t *cmcf;
    ngx_uint_t phase;

    ASSERT(test_cycles_used < sizeof(test_cycles) / sizeof(test_cycles[0]));

    cycle = &test_cycles[test_cycles_used++];
    cycle->modules = modules;

    pool = ngx_create_pool(0, NULL);
    ASSERT(pool != NULL);

    cmcf = ngx_pcalloc(pool, sizeof(ngx_http_core_main_conf_t));
    ASSERT(cmcf != NULL);

    for (phase = 0; phase <= NGX_HTTP_LOG_PHASE; phase++) {
        if (phase == NGX_HTTP_FIND_CONFIG_PHASE
            || phase == NGX_HTTP_POST_REWRITE_PHASE
            || phase == NGX_HTTP_POST_ACCESS_PHASE)
        {
            continue;
        }

        ASSERT(ngx_array_init(&cmcf->phases[phase].handlers, pool, 4,
                              sizeof(ngx_http_handler_pt))
               == NGX_OK);
    }

    ngx_memzero(&cf, sizeof(cf));
    cf.cycle = cycle;
    cf.pool = pool;
    cf.temp_pool = pool;
    cf.ctx = cmcf;

    return cf;
}


TEST(add_handler_allows_the_four_registrable_phases){
    ngx_conf_t cf;
    ngx_http_core_main_conf_t *cmcf;
    ngx_uint_t allowed[] = {
        NGX_HTTP_POST_READ_PHASE,
        NGX_HTTP_PREACCESS_PHASE,
        NGX_HTTP_ACCESS_PHASE,
        NGX_HTTP_PRECONTENT_PHASE
    };
    ngx_uint_t i, before;

    cf = make_test_conf(modules_with_registry);
    cmcf = cf.ctx;

    for (i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
        before = cmcf->phases[allowed[i]].handlers.nelts;

        ASSERT_EQ_INT(nxe_phase_add_handler(&cf, allowed[i], 100,
                                            dummy_handler, "allowed"),
                      NGX_OK);
        ASSERT_EQ_INT(cmcf->phases[allowed[i]].handlers.nelts, before + 1);
    }

    ngx_destroy_pool(cf.pool);
}


TEST(add_handler_rejects_unsupported_phases){
    ngx_conf_t cf;
    ngx_uint_t rejected[] = {
        NGX_HTTP_SERVER_REWRITE_PHASE,
        NGX_HTTP_FIND_CONFIG_PHASE,
        NGX_HTTP_REWRITE_PHASE,
        NGX_HTTP_POST_REWRITE_PHASE,
        NGX_HTTP_POST_ACCESS_PHASE,
        NGX_HTTP_CONTENT_PHASE,
        NGX_HTTP_LOG_PHASE
    };
    ngx_uint_t i;

    cf = make_test_conf(modules_with_registry);

    for (i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++) {
        ASSERT_EQ_INT(nxe_phase_add_handler(&cf, rejected[i], 100,
                                            dummy_handler, "rejected"),
                      NGX_ERROR);
    }

    ngx_destroy_pool(cf.pool);
}


/*
 * Verified via return value and nelts, not by observing a crash: the
 * stub's ngx_array_push() is malloc-based and may not reproduce real
 * nginx's NULL-pool crash, so the test must not depend on crashing.
 */
TEST(add_handler_rejects_before_touching_uninitialized_phase_arrays){
    ngx_conf_t cf;
    ngx_http_core_main_conf_t *cmcf;
    ngx_uint_t uninitialized[] = {
        NGX_HTTP_FIND_CONFIG_PHASE,
        NGX_HTTP_POST_REWRITE_PHASE,
        NGX_HTTP_POST_ACCESS_PHASE
    };
    ngx_uint_t i;

    cf = make_test_conf(modules_with_registry);
    cmcf = cf.ctx;

    for (i = 0; i < sizeof(uninitialized) / sizeof(uninitialized[0]); i++) {
        ASSERT_EQ_INT(nxe_phase_add_handler(&cf, uninitialized[i], 100,
                                            dummy_handler, "uninitialized"),
                      NGX_ERROR);
        ASSERT_EQ_INT(cmcf->phases[uninitialized[i]].handlers.nelts, 0);
        ASSERT(cmcf->phases[uninitialized[i]].handlers.elts == NULL);
    }

    ngx_destroy_pool(cf.pool);
}


TEST(add_handler_reject_log_includes_phase_number){
    ngx_conf_t cf;

    cf = make_test_conf(modules_with_registry);

    ngx_stub_last_log_reset();
    ASSERT_EQ_INT(nxe_phase_add_handler(&cf, NGX_HTTP_LOG_PHASE, 100,
                                        dummy_handler, "logtest"),
                  NGX_ERROR);
    ASSERT(strstr(ngx_stub_last_log(), "10") != NULL);

    ngx_destroy_pool(cf.pool);
}


TEST(add_handler_rejects_via_fallback_path_without_registry){
    ngx_conf_t cf;
    ngx_http_core_main_conf_t *cmcf;

    cf = make_test_conf(modules_without_registry);
    cmcf = cf.ctx;

    ASSERT_EQ_INT(nxe_phase_add_handler(&cf, NGX_HTTP_CONTENT_PHASE, 100,
                                        dummy_handler, "fallback"),
                  NGX_ERROR);
    ASSERT_EQ_INT(cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers.nelts, 0);

    ASSERT_EQ_INT(nxe_phase_add_handler(&cf, NGX_HTTP_ACCESS_PHASE, 100,
                                        dummy_handler, "fallback"),
                  NGX_OK);
    ASSERT_EQ_INT(cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers.nelts, 1);

    ngx_destroy_pool(cf.pool);
}


TEST(add_handler_fallback_path_logs_without_registry){
    ngx_conf_t cf;

    cf = make_test_conf(modules_without_registry);

    ngx_stub_last_log_reset();
    ASSERT_EQ_INT(nxe_phase_add_handler(&cf, NGX_HTTP_ACCESS_PHASE, 100,
                                        dummy_handler, "unordered"),
                  NGX_OK);
    ASSERT(strstr(ngx_stub_last_log(), "no registry module found")
           != NULL);
    ASSERT(strstr(ngx_stub_last_log(), "unordered") != NULL);

    ngx_destroy_pool(cf.pool);
}


TEST(add_handler_rejects_with_null_name_without_crashing){
    ngx_conf_t cf;

    cf = make_test_conf(modules_with_registry);

    ASSERT_EQ_INT(nxe_phase_add_handler(&cf, NGX_HTTP_LOG_PHASE, 100,
                                        dummy_handler, NULL),
                  NGX_ERROR);

    ngx_destroy_pool(cf.pool);
}


/*
 * Regression test for the failed-reload / reused-cycle-address bug:
 * nxe_phase_registry_add() used to key its reset-detection purely on
 * "registry.cycle != cf->cycle" pointer identity, with nothing to
 * invalidate that pointer once the pool it came from was destroyed.
 * A later cycle allocated at the same (freed) address would then be
 * mistaken for the same generation, skipping the reset and pushing
 * into a slots array backed by freed memory.
 *
 * This does not use make_test_conf()/test_cycles -- it needs a single
 * ngx_cycle_t address reused across two independent pools/cmcf, which
 * is exactly the scenario the fix (a pool cleanup registered in
 * nxe_phase_registry_reset()) has to handle.
 */
TEST(pool_cleanup_invalidates_registry_for_reused_cycle_address){
    ngx_conf_t cf;
    ngx_cycle_t cycle;
    ngx_pool_t *pool;
    ngx_http_core_main_conf_t *cmcf;
    ngx_uint_t phase;

    ngx_memzero(&cycle, sizeof(cycle));
    cycle.modules = modules_with_registry;

    /* Generation 1: bind the registry to &cycle. */
    pool = ngx_create_pool(0, NULL);
    ASSERT(pool != NULL);

    cmcf = ngx_pcalloc(pool, sizeof(ngx_http_core_main_conf_t));
    ASSERT(cmcf != NULL);

    for (phase = 0; phase <= NGX_HTTP_LOG_PHASE; phase++) {
        if (phase == NGX_HTTP_FIND_CONFIG_PHASE
            || phase == NGX_HTTP_POST_REWRITE_PHASE
            || phase == NGX_HTTP_POST_ACCESS_PHASE)
        {
            continue;
        }

        ASSERT(ngx_array_init(&cmcf->phases[phase].handlers, pool, 4,
                              sizeof(ngx_http_handler_pt))
               == NGX_OK);
    }

    ngx_memzero(&cf, sizeof(cf));
    cf.cycle = &cycle;
    cf.pool = pool;
    cf.temp_pool = pool;
    cf.ctx = cmcf;

    ASSERT_EQ_INT(nxe_phase_add_handler(&cf, NGX_HTTP_ACCESS_PHASE, 100,
                                        dummy_handler, "gen1"),
                  NGX_OK);
    ASSERT(nxe_phase_registry.cycle == &cycle);

    /*
     * Simulates ngx_destroy_cycle_pools() unwinding a cycle that
     * failed to finish starting. The pool cleanup added by
     * nxe_phase_registry_reset() must fire here and null out
     * registry.cycle, since it still points at this generation.
     */
    ngx_destroy_pool(pool);

    ASSERT(nxe_phase_registry.cycle == NULL);

    /*
     * Generation 2 reuses the exact same cycle address (as a fixed-
     * size cycle pool allocator commonly would after generation 1's
     * pool was freed). Because the cleanup already invalidated the
     * registry, this add() must run a full reset instead of reusing
     * generation 1's now-dangling slots array.
     */
    pool = ngx_create_pool(0, NULL);
    ASSERT(pool != NULL);

    cmcf = ngx_pcalloc(pool, sizeof(ngx_http_core_main_conf_t));
    ASSERT(cmcf != NULL);

    for (phase = 0; phase <= NGX_HTTP_LOG_PHASE; phase++) {
        if (phase == NGX_HTTP_FIND_CONFIG_PHASE
            || phase == NGX_HTTP_POST_REWRITE_PHASE
            || phase == NGX_HTTP_POST_ACCESS_PHASE)
        {
            continue;
        }

        ASSERT(ngx_array_init(&cmcf->phases[phase].handlers, pool, 4,
                              sizeof(ngx_http_handler_pt))
               == NGX_OK);
    }

    cf.pool = pool;
    cf.temp_pool = pool;
    cf.ctx = cmcf;

    ASSERT_EQ_INT(nxe_phase_add_handler(&cf, NGX_HTTP_ACCESS_PHASE, 100,
                                        dummy_handler, "gen2"),
                  NGX_OK);
    ASSERT_EQ_INT(cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers.nelts, 1);
    ASSERT(nxe_phase_registry.cycle == &cycle);
    ASSERT_EQ_INT(
        nxe_phase_registry.groups[NGX_HTTP_ACCESS_PHASE].slots.nelts, 1);

    ngx_destroy_pool(pool);
}


int
main(void)
{
    if (getenv("NXE_PHASE_TEST_VERBOSE") != NULL) {
        verbose = 1;
    }

    ngx_stub_log_set_verbose(verbose);

    /*
     * Real nginx assigns ngx_module_t.name at startup (ngx_preinit_
     * modules(), nginx src/core/ngx_module.c), not via the static
     * initializer NGX_MODULE_V1 (which leaves it NULL). Simulate that
     * one-time assignment here so nxe_phase_add_handler()'s registry
     * discovery (prefix match on m->name) can find this translation
     * unit's own registry module instance.
     */
    NXE_PHASE_MODULE_SYM.name = (char *) NXE_PHASE_STR(NXE_PHASE_MODULE_SYM);

    RUN(ascending_priority_sorts_descending_by_prio);
    RUN(same_priority_sorts_descending_by_seq);
    RUN(mixed_priority_and_ties);
    RUN(does_not_touch_memory_outside_the_given_range);
    RUN(result_independent_of_input_order);
    RUN(empty_array);
    RUN(single_element);
    RUN(all_equal_priority_preserves_seq_derived_order);

    RUN(add_handler_allows_the_four_registrable_phases);
    RUN(add_handler_rejects_unsupported_phases);
    RUN(add_handler_rejects_before_touching_uninitialized_phase_arrays);
    RUN(add_handler_reject_log_includes_phase_number);
    RUN(add_handler_rejects_via_fallback_path_without_registry);
    RUN(add_handler_fallback_path_logs_without_registry);
    RUN(add_handler_rejects_with_null_name_without_crashing);
    RUN(pool_cleanup_invalidates_registry_for_reused_cycle_address);

    printf("%d/%d tests passed\n", tests_run - tests_failed, tests_run);

    return tests_failed == 0 ? 0 : 1;
}

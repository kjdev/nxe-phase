/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_phase.c - registry implementation backing nxe_phase_add_handler()
 *
 * Every symbol here is static except the ngx_module_t instance itself
 * (NXE_PHASE_MODULE_SYM, built from NXE_PHASE_TAG) -- this file is
 * vendored and compiled into several independent .so files / addon
 * trees, one per consuming module, so nothing here may collide with
 * another copy of the same file linked elsewhere in the same process.
 *
 * This file is never added to ngx_module_srcs directly.  config.ngx
 * generates a per-consumer wrapper .c under $NGX_OBJS that #defines
 * NXE_PHASE_TAG and #includes this file by absolute path, so each
 * consumer gets its own translation unit -- and therefore its own
 * object file path and its own tag -- without NXE_PHASE_TAG leaking
 * into any other consumer's build through a shared CFLAGS.
 */

#include "nxe_phase.h"


/*
 * A tracked handler plus the array index it occupies in
 * cmcf->phases[phase].handlers at the time it was pushed.  Because
 * ngx_array_push() always appends, that index only ever grows across
 * successive calls into this phase's group, so the slot array below is
 * implicitly kept in ascending-index order without needing a sort of
 * its own.
 */
typedef struct {
    nxe_phase_entry_t  entry;
    ngx_uint_t         idx;
} nxe_phase_slot_t;

typedef struct {
    ngx_array_t  slots;   /* of nxe_phase_slot_t, valid once cycle != NULL */
} nxe_phase_phase_group_t;

typedef struct {
    ngx_cycle_t             *cycle;
    ngx_pool_t              *pool;
    ngx_uint_t               seq;
    nxe_phase_phase_group_t  groups[NGX_HTTP_LOG_PHASE + 1];
} nxe_phase_registry_t;


/*
 * Zero-initialized; the first real add() call for a given cycle
 * populates cycle/pool and re-runs ngx_array_init() for every phase
 * group's slots array, discarding whatever an earlier cycle (e.g.
 * before a config reload) left behind.
 */
static nxe_phase_registry_t nxe_phase_registry;


static ngx_int_t
nxe_phase_sort_entries(nxe_phase_entry_t *entries, ngx_uint_t n)
{
    ngx_uint_t i, j;
    nxe_phase_entry_t key;

    /*
     * Insertion sort, descending by (prio, seq).  This is the order in
     * which entries are written back into ascending array-index slots
     * of cmcf->phases[phase].handlers.  ngx_http_init_phase_handlers()
     * (nginx src/http/ngx_http.c) walks that source array from its
     * tail to its head when building the runtime phase engine, so the
     * highest array index runs first.  Placing the lowest prio value
     * (highest precedence) at the highest index, and -- among equal
     * priorities -- the earliest-registered entry (lowest seq) at the
     * highest index, makes execution order ascend by prio and, within
     * a tie, follow registration order.
     */
    for (i = 1; i < n; i++) {
        key = entries[i];
        j = i;

        while (j > 0
               && (entries[j - 1].prio < key.prio
                   || (entries[j - 1].prio == key.prio
                       && entries[j - 1].seq < key.seq)))
        {
            entries[j] = entries[j - 1];
            j--;
        }

        entries[j] = key;
    }

    return NGX_OK;
}


/*
 * Pool cleanup handler for the cf->pool bound in nxe_phase_registry_
 * reset(). That pool -- and everything allocated from it, including
 * every phase group's slots array -- is destroyed both when a reload
 * succeeds (the *old* cycle's pool is torn down) and when one fails
 * partway through (ngx_destroy_cycle_pools() unwinds the *new*
 * cycle's pool). Either way, "data" is the ngx_cycle_t this cleanup
 * was registered for; only null out the registry's cycle pointer if
 * it still refers to that same generation. On the common successful-
 * reload path the registry has already moved on to the new cycle by
 * the time the old one's pool is destroyed, so this is a no-op there
 * and only fires for the failed-reload case.
 *
 * Without this, nxe_phase_registry_add()'s "cycle != cf->cycle" check
 * is the only generation guard, and a subsequent cycle allocated at
 * the same address (cycle pools are a fixed-size ngx_create_pool(),
 * so reuse after a free is common, not theoretical) would be
 * mistaken for the same generation: the reset would be skipped and
 * ngx_array_push() would write into the slots array through a
 * dangling elts pointer into the destroyed pool.
 */
static void
nxe_phase_registry_invalidate(void *data)
{
    if (nxe_phase_registry.cycle == data) {
        nxe_phase_registry.cycle = NULL;
    }
}


static ngx_int_t
nxe_phase_registry_reset(ngx_conf_t *cf)
{
    ngx_uint_t phase;
    ngx_pool_cleanup_t *cln;

    cln = ngx_pool_cleanup_add(cf->pool, 0);
    if (cln == NULL) {
        return NGX_ERROR;
    }

    cln->handler = nxe_phase_registry_invalidate;
    cln->data = cf->cycle;

    nxe_phase_registry.cycle = cf->cycle;
    nxe_phase_registry.pool = cf->pool;
    nxe_phase_registry.seq = 0;

    for (phase = 0; phase <= NGX_HTTP_LOG_PHASE; phase++) {
        if (ngx_array_init(&nxe_phase_registry.groups[phase].slots,
                           cf->pool, 4, sizeof(nxe_phase_slot_t))
            != NGX_OK)
        {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static ngx_int_t
nxe_phase_registry_add(ngx_conf_t *cf, ngx_uint_t phase, ngx_int_t prio,
    ngx_http_handler_pt h, const char *name)
{
    ngx_uint_t i, n;
    ngx_array_t *slots;
    nxe_phase_slot_t *slot;
    nxe_phase_entry_t *buf;
    ngx_http_handler_pt *hp, *elts;
    ngx_http_core_main_conf_t *cmcf;

    /*
     * Re-validated here even though nxe_phase_add_handler() (nxe_phase.h)
     * already checks phase: this function can be reached through another
     * vendored copy's header via the authority->api.add() function
     * pointer. If that older copy predates this check, the header-side
     * validation is bypassed entirely -- this is the last line of defense
     * against a version-mixing scenario, not redundant with the header check.
     */
    if (nxe_phase_check_phase(cf, phase, name) != NGX_OK) {
        return NGX_ERROR;
    }

    if (nxe_phase_registry.cycle != cf->cycle) {
        if (nxe_phase_registry_reset(cf) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    hp = ngx_array_push(&cmcf->phases[phase].handlers);
    if (hp == NULL) {
        return NGX_ERROR;
    }

    *hp = h;

    slots = &nxe_phase_registry.groups[phase].slots;

    slot = ngx_array_push(slots);
    if (slot == NULL) {
        return NGX_ERROR;
    }

    slot->entry.handler = h;
    slot->entry.prio = prio;
    slot->entry.seq = nxe_phase_registry.seq++;
    slot->entry.name = name;
    slot->idx = cmcf->phases[phase].handlers.nelts - 1;

    n = slots->nelts;

    buf = ngx_palloc(cf->temp_pool, n * sizeof(nxe_phase_entry_t));
    if (buf == NULL) {
        return NGX_ERROR;
    }

    for (i = 0; i < n; i++) {
        buf[i] = ((nxe_phase_slot_t *) slots->elts)[i].entry;
    }

    if (nxe_phase_sort_entries(buf, n) != NGX_OK) {
        return NGX_ERROR;
    }

    elts = cmcf->phases[phase].handlers.elts;

    for (i = 0; i < n; i++) {
        elts[((nxe_phase_slot_t *) slots->elts)[i].idx] = buf[i].handler;
    }

    return NGX_OK;
}


static nxe_phase_module_ctx_t nxe_phase_module_ctx = {
    {
        NULL,                          /* preconfiguration */
        NULL,                          /* postconfiguration */

        NULL,                          /* create main configuration */
        NULL,                          /* init main configuration */

        NULL,                          /* create server configuration */
        NULL,                          /* merge server configuration */

        NULL,                          /* create location configuration */
        NULL                           /* merge location configuration */
    },
    {
        NXE_PHASE_API_MAGIC,
        NXE_PHASE_API_VERSION,
        nxe_phase_registry_add
    }
};


ngx_module_t NXE_PHASE_MODULE_SYM = {
    NGX_MODULE_V1,
    &nxe_phase_module_ctx,             /* module context */
    NULL,                              /* module directives */
    NGX_HTTP_MODULE,                   /* module type */
    NULL,                              /* init master */
    NULL,                              /* init module */
    NULL,                              /* init process */
    NULL,                              /* init thread */
    NULL,                              /* exit thread */
    NULL,                              /* exit process */
    NULL,                              /* exit master */
    NGX_MODULE_V1_PADDING
};

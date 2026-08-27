/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * ngx_http.h - minimal ngx_http.h stub for nxe-phase unit tests
 *
 * nxe_phase_sort_entries() is a static function inside src/nxe_phase.c,
 * so exercising it means compiling that whole translation unit -- test_
 * runner.c pulls it in via #include (see tests/unit/test_runner.c).
 * That drags in every ngx_http.h-dependent declaration around it even
 * though no test case here calls through them at runtime.  This stub
 * supplies just enough of the real ngx_http.h/ngx_module.h surface for
 * that to compile; field layouts mirror nginx where a struct is used
 * with positional initializers (ngx_module_t) so those initializers
 * stay valid.
 */

#ifndef NGX_HTTP_H_STUB
#define NGX_HTTP_H_STUB

#include "ngx_stub.h"


typedef struct ngx_cycle_s ngx_cycle_t;
typedef struct ngx_conf_s ngx_conf_t;
typedef struct ngx_module_s ngx_module_t;
typedef struct ngx_http_request_s ngx_http_request_t;

typedef ngx_int_t (*ngx_http_handler_pt)(ngx_http_request_t *r);


/* --- ngx_module_t (subset of nginx core/ngx_module.h) --- */

#define NGX_MODULE_UNSET_INDEX  ((ngx_uint_t) -1)

#define NGX_MODULE_V1 \
        NGX_MODULE_UNSET_INDEX, NGX_MODULE_UNSET_INDEX, NULL, 0, 0, 1, ""

#define NGX_MODULE_V1_PADDING  0, 0, 0, 0, 0, 0, 0, 0

#define NGX_HTTP_MODULE  0x50545448  /* "HTTP" */

struct ngx_module_s {
    ngx_uint_t  ctx_index;
    ngx_uint_t  index;
    char       *name;
    ngx_uint_t  spare0;
    ngx_uint_t  spare1;
    ngx_uint_t  version;
    const char *signature;

    void       *ctx;
    void       *commands;
    ngx_uint_t  type;

    ngx_int_t   (*init_master)(ngx_log_t *log);
    ngx_int_t   (*init_module)(ngx_cycle_t *cycle);
    ngx_int_t   (*init_process)(ngx_cycle_t *cycle);
    ngx_int_t   (*init_thread)(ngx_cycle_t *cycle);
    void        (*exit_thread)(ngx_cycle_t *cycle);
    void        (*exit_process)(ngx_cycle_t *cycle);
    void        (*exit_master)(ngx_cycle_t *cycle);

    uintptr_t   spare_hook0;
    uintptr_t   spare_hook1;
    uintptr_t   spare_hook2;
    uintptr_t   spare_hook3;
    uintptr_t   spare_hook4;
    uintptr_t   spare_hook5;
    uintptr_t   spare_hook6;
    uintptr_t   spare_hook7;
};


/* --- ngx_http_module_t (subset of nginx http/ngx_http_config.h) --- */

typedef struct {
    ngx_int_t (*preconfiguration)(ngx_conf_t *cf);
    ngx_int_t (*postconfiguration)(ngx_conf_t *cf);

    void *(*create_main_conf)(ngx_conf_t *cf);
    char *(*init_main_conf)(ngx_conf_t *cf, void *conf);

    void *(*create_srv_conf)(ngx_conf_t *cf);
    char *(*merge_srv_conf)(ngx_conf_t *cf, void *prev, void *conf);

    void *(*create_loc_conf)(ngx_conf_t *cf);
    char *(*merge_loc_conf)(ngx_conf_t *cf, void *prev, void *conf);
} ngx_http_module_t;


/* --- phase handler storage (subset of nginx http/ngx_http_core_module.h) --- */

#define NGX_HTTP_LOG_PHASE  10

typedef struct {
    ngx_array_t  handlers;
} ngx_http_phase_t;

typedef struct {
    ngx_http_phase_t  phases[NGX_HTTP_LOG_PHASE + 1];
} ngx_http_core_main_conf_t;

/*
 * Real nginx resolves this through cf->ctx (an ngx_http_conf_ctx_t) and
 * the module's ctx_index; the "module" argument only exists to select
 * which slot of that ctx to read. No unit test here ever calls a code
 * path where more than one HTTP module's main conf coexists, so cf->ctx
 * is used directly as the ngx_http_core_main_conf_t and "module" is
 * discarded.
 */
#define ngx_http_conf_get_module_main_conf(cf, module) \
        ((ngx_http_core_main_conf_t *) (cf)->ctx)


/* --- ngx_conf_t / ngx_cycle_t (subset) --- */

struct ngx_conf_s {
    ngx_cycle_t *cycle;
    ngx_pool_t  *pool;
    ngx_pool_t  *temp_pool;
    void        *ctx;
};

struct ngx_cycle_s {
    ngx_module_t **modules;   /* NULL-terminated */
};


#endif /* NGX_HTTP_H_STUB */

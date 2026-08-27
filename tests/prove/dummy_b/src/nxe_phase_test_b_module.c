/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_phase_test_b_module.c - dummy PREACCESS handler for tests/prove
 *
 * Registers through nxe_phase_add_handler() with a priority higher than
 * dummy_a's, so it must always run second regardless of the order the
 * two .so files are given to TEST_NGINX_LOAD_MODULES -- that is the
 * property tests/prove/phase_order.t exercises.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "nxe_phase.h"

/*
 * Pins the four registrable phases' numeric values against the real
 * nginx headers this file compiles against (unlike tests/unit/ngx_compat/
 * ngx_http.h, which hand-mirrors ngx_http_phases for the stub-based C
 * unit tests). If upstream nginx ever renumbers ngx_http_phases, this
 * fails to compile here first instead of the unit test stub silently
 * drifting out of sync. _Static_assert (not #if) because ngx_http_phases
 * is a C enum, not visible to the preprocessor.
 */
_Static_assert(NGX_HTTP_POST_READ_PHASE == 0,
               "nxe_phase: NGX_HTTP_POST_READ_PHASE drifted from "
               "tests/unit/ngx_compat/ngx_http.h -- update both");
_Static_assert(NGX_HTTP_PREACCESS_PHASE == 5,
               "nxe_phase: NGX_HTTP_PREACCESS_PHASE drifted from "
               "tests/unit/ngx_compat/ngx_http.h -- update both");
_Static_assert(NGX_HTTP_ACCESS_PHASE == 6,
               "nxe_phase: NGX_HTTP_ACCESS_PHASE drifted from "
               "tests/unit/ngx_compat/ngx_http.h -- update both");
_Static_assert(NGX_HTTP_PRECONTENT_PHASE == 8,
               "nxe_phase: NGX_HTTP_PRECONTENT_PHASE drifted from "
               "tests/unit/ngx_compat/ngx_http.h -- update both");

/* Arbitrary, test-only priorities -- lower runs first. */
#define NXE_PHASE_TEST_B_PRIO  200

static ngx_int_t nxe_phase_test_b_postconf(ngx_conf_t *cf);
static ngx_int_t nxe_phase_test_b_handler(ngx_http_request_t *r);

static ngx_http_module_t nxe_phase_test_b_module_ctx = {
    NULL,                       /* preconfiguration */
    nxe_phase_test_b_postconf,  /* postconfiguration */
    NULL,                       /* create main configuration */
    NULL,                       /* init main configuration */
    NULL,                       /* create server configuration */
    NULL,                       /* merge server configuration */
    NULL,                       /* create location configuration */
    NULL                        /* merge location configuration */
};

ngx_module_t nxe_phase_test_b_module = {
    NGX_MODULE_V1,
    &nxe_phase_test_b_module_ctx, /* module context */
    NULL,                         /* module directives */
    NGX_HTTP_MODULE,              /* module type */
    NULL,                         /* init master */
    NULL,                         /* init module */
    NULL,                         /* init process */
    NULL,                         /* init thread */
    NULL,                         /* exit thread */
    NULL,                         /* exit process */
    NULL,                         /* exit master */
    NGX_MODULE_V1_PADDING
};

/*
 * Appends ",b" (or sets the header to "b" if absent) to the shared
 * X-Nxe-Phase-Order response header, so the final header value records
 * the order handlers actually ran in.
 */
static ngx_int_t
nxe_phase_test_b_handler(ngx_http_request_t *r)
{
    u_char *p;
    size_t old_len;
    ngx_str_t tag;
    ngx_uint_t i;
    ngx_flag_t found;
    ngx_list_part_t *part;
    ngx_table_elt_t *h, *header;

    ngx_str_set(&tag, "b");

    h = NULL;
    found = 0;
    part = &r->headers_out.headers.part;
    header = part->elts;

    for (i = 0; /* void */; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            header = part->elts;
            i = 0;
        }

        if (header[i].hash == 0) {
            continue;
        }

        if (header[i].key.len == sizeof("X-Nxe-Phase-Order") - 1
            && ngx_strncasecmp(header[i].key.data,
                               (u_char *) "X-Nxe-Phase-Order",
                               header[i].key.len)
            == 0)
        {
            h = &header[i];
            found = 1;
            break;
        }
    }

    if (found) {
        old_len = h->value.len;

        p = ngx_pnalloc(r->pool, old_len + 1 + tag.len);
        if (p == NULL) {
            return NGX_ERROR;
        }

        ngx_memcpy(p, h->value.data, old_len);
        p[old_len] = ',';
        ngx_memcpy(p + old_len + 1, tag.data, tag.len);

        h->value.data = p;
        h->value.len = old_len + 1 + tag.len;

        return NGX_DECLINED;
    }

    h = ngx_list_push(&r->headers_out.headers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    h->next = NULL;
    h->hash = 1;
    ngx_str_set(&h->key, "X-Nxe-Phase-Order");
    h->value = tag;

    return NGX_DECLINED;
}

static ngx_int_t
nxe_phase_test_b_postconf(ngx_conf_t *cf)
{
    return nxe_phase_add_handler(cf, NGX_HTTP_PREACCESS_PHASE,
                                 NXE_PHASE_TEST_B_PRIO,
                                 nxe_phase_test_b_handler,
                                 "nxe_phase_test_b_module");
}

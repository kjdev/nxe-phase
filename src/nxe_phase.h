/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_phase.h - shared registry for ordering handlers within an nginx
 * HTTP phase
 *
 * nginx has no mechanism to control the execution order of multiple
 * handlers registered in the same phase by different modules.
 * ngx_http_init_phase_handlers() (nginx src/http/ngx_http.c) walks the
 * per-phase handlers array from its tail to its head, so the effective
 * execution order is the reverse of the order handlers were pushed
 * during postconfiguration -- which in turn follows --add-module /
 * load_module order.  This ties authorization semantics to build
 * arguments instead of nginx.conf.
 *
 * nxe_phase_add_handler() replaces a direct ngx_array_push() call.  It
 * registers the handler in a shared priority-ordered registry and
 * re-sorts only the slots that registry owns inside the phase's
 * handlers array, every time a new handler is added.  Lower priority
 * numbers run first (see the NXE_PHASE_PRIO_* band below).
 *
 * IMPORTANT -- HTTP only, not usable from stream.  Unlike the other
 * nxe-* submodules (nxe-json, nxe-cedar, nxe-jwx), which depend only on
 * nginx core, nxe-phase depends on ngx_http.h (ngx_http_module_t,
 * ngx_http_handler_pt, ngx_http_core_main_conf_t) because it directly
 * manipulates the HTTP phase handler arrays.  This is an intentional,
 * documented exception -- see .claude/rules/c-source.md.
 *
 * Return value contract for handlers registered through this API:
 *   - generic phases (POST_READ / PREACCESS / PRECONTENT): return
 *     NGX_DECLINED on success too.  NGX_OK terminates the *whole*
 *     phase (r->phase_handler jumps to ph->next), not just the current
 *     handler, so returning it on success would skip every other
 *     handler still queued in the phase.  Only return NGX_OK when the
 *     phase should be cut short on purpose.
 *   - ACCESS phase: NGX_OK on success is fine.  ngx_http_core_access_
 *     phase() advances to the next handler under the default
 *     "satisfy all" and only short-circuits under "satisfy any",
 *     which is the documented meaning of that directive.
 *
 * Depends on: nginx core, ngx_http.h.
 */

#ifndef _NXE_PHASE_H_INCLUDED_
#define _NXE_PHASE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/*
 * Registry discovery magic/version.
 *
 * nxe-phase is vendored into every consuming module's build, so the
 * same code can be compiled into several independent .so files / addon
 * trees.  All external linkage is reduced to a single ngx_module_t per
 * tag (NXE_PHASE_TAG) so ngx_add_module()'s name-uniqueness check and
 * static-build symbol resolution both succeed.  Other modules discover
 * the (arbitrarily chosen, functionally interchangeable) registry
 * instance by scanning cf->cycle->modules for a name starting with
 * "nxe_phase_order_module_" and reading an extension ctx appended after
 * the mandatory ngx_http_module_t header.
 */
#define NXE_PHASE_API_MAGIC     0x4e584550  /* "NXEP" */
#define NXE_PHASE_API_VERSION   1

#define NXE_PHASE_MODULE_NAME_PREFIX      "nxe_phase_order_module_"
#define NXE_PHASE_MODULE_NAME_PREFIX_LEN  \
        (sizeof(NXE_PHASE_MODULE_NAME_PREFIX) - 1)


/*
 * Priority bands.
 *
 * Values are shared across every consuming module, not chosen per
 * module, because ordering is an ecosystem-wide agreement.
 * Bands are spaced 100 apart to leave room for future
 * modules without renumbering existing ones.  Lower values run first.
 */

/* 100-199: transport / request-signature verification */
#define NXE_PHASE_PRIO_HTTPSIG              150

/* 200-299: bearer-token authentication */
#define NXE_PHASE_PRIO_JWT                  200
#define NXE_PHASE_PRIO_OAUTH2_TOKEN         250

/* 300-399: API-key authentication */
#define NXE_PHASE_PRIO_APIKEY               300

/* 400-499: WebAuthn/FIDO2 authentication */
#define NXE_PHASE_PRIO_WEBAUTHN             450

/* 500-599: OpenID Connect session resolution */
#define NXE_PHASE_PRIO_OIDC                 500

/* 600-699: policy gate */
#define NXE_PHASE_PRIO_GATE                 600

/* 700-799: fine-grained authorization */
#define NXE_PHASE_PRIO_CEDAR                700
#define NXE_PHASE_PRIO_RBAC                 750

/* 800-899: quota / throttling */
#define NXE_PHASE_PRIO_RATELIMIT            800

/* 900-999: routing side effects */
#define NXE_PHASE_PRIO_INTERNAL_REDIRECT    900


/*
 * nxe_phase_api_t / nxe_phase_module_ctx_t
 *
 * ngx_module_t.ctx for an HTTP module is read by nginx core as an
 * ngx_http_module_t; nginx never reads past its end.  Appending
 * nxe_phase_api_t after it lets other modules reach the registry's
 * "add" entry point through the ordinary ngx_module_t.ctx pointer,
 * without needing a symbol of their own to link against.
 */
typedef struct {
    ngx_uint_t  magic;       /* NXE_PHASE_API_MAGIC */
    ngx_uint_t  version;     /* NXE_PHASE_API_VERSION */
    ngx_int_t (*add)(ngx_conf_t *cf, ngx_uint_t phase, ngx_int_t prio,
        ngx_http_handler_pt h, const char *name);
} nxe_phase_api_t;

typedef struct {
    ngx_http_module_t  http;   /* fixed first member -- read by nginx core */
    nxe_phase_api_t    api;
} nxe_phase_module_ctx_t;


/*
 * A single registered handler, as tracked by the registry.
 */
typedef struct {
    ngx_http_handler_pt  handler;
    ngx_int_t            prio;
    ngx_uint_t           seq;    /* registration order, for stable sort */
    const char          *name;
} nxe_phase_entry_t;


/*
 * Token-pasting helpers to build the per-tag ngx_module_t symbol name
 * from the NXE_PHASE_TAG macro.  config.ngx defines it inside a
 * generated per-consumer wrapper .c (under $NGX_OBJS) that #includes
 * this file's companion nxe_phase.c, rather than passing
 * -DNXE_PHASE_TAG=<tag> through CFLAGS -- see config.ngx for why.
 */
#define NXE_PHASE_CONCAT_(a, b)  a ## b
#define NXE_PHASE_CONCAT(a, b)   NXE_PHASE_CONCAT_(a, b)

#define NXE_PHASE_MODULE_SYM \
        NXE_PHASE_CONCAT(nxe_phase_order_module_, NXE_PHASE_TAG)


/*
 * nxe_phase_check_phase() -- validate that "phase" is one of the four
 * phases this registry supports, logging a diagnostic and rejecting
 * everything else.
 *
 * Two problems make an unrestricted "phase" argument unsafe:
 *
 *   1. ngx_http_init_phases() (nginx src/http/ngx_http.c) only calls
 *      ngx_array_init() for 8 of the 11 phases. FIND_CONFIG /
 *      POST_REWRITE / POST_ACCESS are left zeroed (cmcf comes from
 *      ngx_pcalloc()), so ngx_array_push()-ing into one of them hits
 *      the nelts == nalloc == 0 growth path with elts == NULL and
 *      crashes during config parsing.
 *   2. ngx_http_log_request() (nginx src/http/ngx_http_request.c)
 *      walks LOG-phase handlers index 0 -> n, bypassing the phase
 *      engine entirely. nxe_phase_sort_entries()'s ordering contract
 *      ("highest priority at the highest array index") assumes the
 *      phase engine's tail-to-head walk, so applying it to
 *      LOG produces the reverse execution order.
 *
 * including why SERVER_REWRITE / REWRITE / CONTENT are excluded even
 * though neither problem applies to them.
 */
static ngx_inline ngx_int_t
nxe_phase_check_phase(ngx_conf_t *cf, ngx_uint_t phase, const char *name)
{
    switch (phase) {
    case NGX_HTTP_POST_READ_PHASE:
    case NGX_HTTP_PREACCESS_PHASE:
    case NGX_HTTP_ACCESS_PHASE:
    case NGX_HTTP_PRECONTENT_PHASE:
        return NGX_OK;

    default:
        break;
    }

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "nxe_phase: handler \"%s\" tried to register on "
                       "unsupported phase %ui -- only POST_READ, "
                       "PREACCESS, ACCESS and PRECONTENT are supported",
                       name ? name : "(unnamed)", phase);

    return NGX_ERROR;
}


/*
 * nxe_phase_add_handler() -- register a phase handler with an explicit
 * priority instead of pushing it directly onto
 * cmcf->phases[phase].handlers.
 *
 * Walks cf->cycle->modules looking for the shared registry (an
 * ngx_module_t whose name starts with NXE_PHASE_MODULE_NAME_PREFIX).
 * Every matching instance's ctx is validated via the magic number
 * before being trusted, and all instances found must agree on
 * "version" -- a mismatch means two vendored copies of nxe-phase
 * disagree on the registry's memory layout, so this fails the
 * configuration outright (NGX_LOG_EMERG) instead of risking silently
 * broken ordering. The first validated instance found is treated as
 * the sole authority; which .so happens to own it does not matter
 * because every instance is functionally identical.
 *
 * If no registry module is found at all (nxe-phase built into a
 * process where it is the only consumer, or a unit-test harness),
 * falls back to a plain ngx_array_push() so callers keep working
 * without the ordering guarantee.
 */
static ngx_inline ngx_int_t
nxe_phase_add_handler(ngx_conf_t *cf, ngx_uint_t phase, ngx_int_t prio,
    ngx_http_handler_pt h, const char *name)
{
    ngx_uint_t i, version;
    ngx_module_t *m;
    nxe_phase_module_ctx_t *ctx, *authority;
    ngx_http_core_main_conf_t *cmcf;
    ngx_http_handler_pt *hp;

    if (nxe_phase_check_phase(cf, phase, name) != NGX_OK) {
        return NGX_ERROR;
    }

    authority = NULL;
    version = 0;

    for (i = 0; cf->cycle->modules[i]; i++) {
        m = cf->cycle->modules[i];

        if (m->type != NGX_HTTP_MODULE) {
            continue;
        }

        if (m->name == NULL
            || ngx_strncmp(m->name, NXE_PHASE_MODULE_NAME_PREFIX,
                           NXE_PHASE_MODULE_NAME_PREFIX_LEN)
            != 0)
        {
            continue;
        }

        ctx = m->ctx;

        if (ctx == NULL || ctx->api.magic != NXE_PHASE_API_MAGIC) {
            continue;
        }

        if (authority == NULL) {
            authority = ctx;
            version = ctx->api.version;

            ngx_conf_log_error(NGX_LOG_DEBUG, cf, 0,
                               "nxe_phase: using registry from module \"%s\" "
                               "(version %ui) for \"%s\"",
                               m->name, version, name ? name : "(unnamed)");
            continue;
        }

        if (ctx->api.version != version) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "nxe_phase: registry version mismatch: module \"%s\" "
                               "reports version %ui, expected %ui (from an earlier "
                               "vendored copy) -- rebuild all modules against the "
                               "same nxe-phase version",
                               m->name, ctx->api.version, version);
            return NGX_ERROR;
        }
    }

    if (authority != NULL) {
        return authority->api.add(cf, phase, prio, h, name);
    }

    /* Fallback: no registry found, register directly without ordering. */

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    hp = ngx_array_push(&cmcf->phases[phase].handlers);
    if (hp == NULL) {
        return NGX_ERROR;
    }

    *hp = h;

    return NGX_OK;
}


#endif /* _NXE_PHASE_H_INCLUDED_ */

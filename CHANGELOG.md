# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Initial phase handler ordering registry for submodule use
  - `nxe_phase_add_handler(cf, phase, prio, handler, name)` replaces a
    direct `ngx_array_push()` call, registering the handler in a
    shared priority-ordered registry that re-sorts only the array
    slots it owns inside `cmcf->phases[phase].handlers`, every time a
    new handler is added
  - Only `POST_READ` / `PREACCESS` / `ACCESS` / `PRECONTENT` are
    accepted; any other phase fails config parsing with
    `NGX_LOG_EMERG`, in both the registry path and the standalone
    fallback path
  - Execution order becomes ascending by priority and, among equal
    priorities, by registration order — independent of
    `--add-module` / `load_module` order, which
    `ngx_http_init_phase_handlers()` otherwise reverses
  - Priority bands centralized in `nxe_phase.h`, spaced 100 apart,
    covering the 11 consuming modules: `NXE_PHASE_PRIO_HTTPSIG`,
    `_JWT`, `_OAUTH2_TOKEN`, `_APIKEY`, `_WEBAUTHN`, `_OIDC`, `_GATE`,
    `_CEDAR`, `_RBAC`, `_RATELIMIT`, `_INTERNAL_REDIRECT`
  - Registry discovery via a tag-scoped `ngx_module_t`
    (`nxe_phase_order_module_<tag>`) and an extension ctx appended
    after the mandatory `ngx_http_module_t`; version mismatches
    between vendored copies abort startup (`NGX_LOG_EMERG`)
  - Falls back to a plain `ngx_array_push()` when built standalone
    (no registry module present in the cycle)
  - `config.ngx` requiring `nxe_phase_dir` / `nxe_phase_tag` to be set
    by the caller before sourcing
- Unit test suite (`tests/unit/`) covering
  `nxe_phase_sort_entries()`: ascending-by-priority execution order,
  stable ordering for equal priorities, slots outside the registry's
  own group left untouched, idempotence under reordered registration,
  and empty / single-element / all-equal-priority inputs
- Test::Nginx integration suite (`tests/prove/`) with two dummy
  PREACCESS-phase modules (`dummy_a`, `dummy_b`) at different
  priorities, verifying that response header order follows priority
  regardless of `TEST_NGINX_LOAD_MODULES` order
- CI workflows for both test suites (`tests/unit/` via `make test` /
  `test-asan` / `test-cov`, `tests/prove/` via `prove` against
  separately-configured dummy module builds in both load orders)

### Fixed

- The registry no longer risks reusing a stale generation after a
  failed reload. It used to key its reset detection purely on cycle
  pointer identity, with nothing to invalidate that pointer once the
  cycle's pool was destroyed; a later cycle allocated at the same
  (freed) address would then be mistaken for the same generation,
  skipping the reset and writing through a dangling pointer into
  freed memory. A pool cleanup registered when the registry binds to
  a cycle now invalidates it as soon as that cycle's pool is torn
  down, so the next `nxe_phase_add_handler()` call always rebuilds
  the registry from scratch instead.
- The standalone fallback path (no registry module found in the
  cycle) no longer fails silently. It still registers the handler via
  a plain `ngx_array_push()`, but now logs a `NGX_LOG_WARN` naming the
  handler, so a build misconfiguration that drops every consumer back
  to `load_module` order is observable instead of a silent fail-open.

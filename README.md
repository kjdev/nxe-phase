# nxe-phase

Shared registry for ordering handlers within an nginx HTTP phase
(NginX Extension Phase ordering), distributed as a git submodule.

## Why this exists

nginx has no mechanism to control the execution order of multiple
handlers registered in the same phase by different modules.
`ngx_http_init_phase_handlers()` (nginx `src/http/ngx_http.c`) walks
each phase's handlers array from its tail to its head, so the
effective execution order is the *reverse* of the order handlers were
pushed during `postconfiguration` — which in turn follows
`--add-module` / `load_module` order. That ties authentication and
authorization semantics to build arguments instead of `nginx.conf`.

nxe-phase replaces a direct `ngx_array_push()` call with
`nxe_phase_add_handler(cf, phase, prio, handler, name)`. It registers
the handler in a shared priority-ordered registry and re-sorts only
the slots that registry owns inside the phase's handlers array, every
time a new handler is added, so execution order is fixed by priority
and independent of registration/build order.

**HTTP only, not usable from stream.** Unlike the other nxe-*
submodules (nxe-json, nxe-cedar, nxe-jwx), which depend only on nginx
core, nxe-phase depends on `ngx_http.h` because it directly
manipulates the HTTP phase handler arrays.

## Features

- **Priority-ordered execution** — handlers registered through
  `nxe_phase_add_handler()` run in ascending priority order within
  their phase, regardless of `--add-module` / `load_module` order.
- **Scoped re-sorting** — only the array slots owned by nxe-phase
  registrations are re-sorted. Slots belonging to other modules
  (`realip`, `limit_req`, `try_files`, ...) are never touched.
- **Idempotent** — the registry re-sorts on every registration, so the
  final order does not depend on which module happens to call
  `postconfiguration` last.
- **Stable ordering** — handlers with equal priority run in
  registration order.
- **Vendored, tag-scoped distribution** — compiles into each
  consuming module's own build; a per-module tag keeps the resulting
  `ngx_module_t` symbol unique across coexisting copies.
- **Fail-closed version check** — if multiple vendored copies of
  nxe-phase are loaded in the same process, a version mismatch aborts
  startup (`NGX_LOG_EMERG`) instead of risking silently broken
  ordering.

## API overview

See [`src/nxe_phase.h`](src/nxe_phase.h) for full documentation.

| Function | Purpose |
|---------|---------|
| `nxe_phase_add_handler(cf, phase, prio, handler, name)` | Register a phase handler with an explicit priority instead of `ngx_array_push()` |

### Priority bands

Values are shared across every consuming module — ordering is an
ecosystem-wide agreement, not a per-module choice. Bands are spaced
100 apart to leave room for future modules without renumbering
existing ones. Lower values run first.

| Band | Constant | Purpose |
|---|---|---|
| 100-199 | `NXE_PHASE_PRIO_HTTPSIG` (150) | Transport / request-signature verification |
| 200-299 | `NXE_PHASE_PRIO_JWT` (200), `NXE_PHASE_PRIO_OAUTH2_TOKEN` (250) | Bearer-token authentication |
| 300-399 | `NXE_PHASE_PRIO_APIKEY` (300) | API-key authentication |
| 400-499 | `NXE_PHASE_PRIO_WEBAUTHN` (450) | WebAuthn/FIDO2 authentication |
| 500-599 | `NXE_PHASE_PRIO_OIDC` (500) | OpenID Connect session resolution |
| 600-699 | `NXE_PHASE_PRIO_GATE` (600) | Policy gate |
| 700-799 | `NXE_PHASE_PRIO_CEDAR` (700), `NXE_PHASE_PRIO_RBAC` (750) | Fine-grained authorization |
| 800-899 | `NXE_PHASE_PRIO_RATELIMIT` (800) | Quota / throttling |
| 900-999 | `NXE_PHASE_PRIO_INTERNAL_REDIRECT` (900) | Routing side effects |

### Return value contract

- **Generic phases** (`POST_READ` / `PREACCESS` / `PRECONTENT`) —
  return `NGX_DECLINED` on success too. `NGX_OK` terminates the whole
  phase (`r->phase_handler` jumps to `ph->next`), not just the current
  handler, so returning it on success would skip every other handler
  still queued in the phase. Only return `NGX_OK` when the phase
  should be cut short on purpose.
- **ACCESS phase** — `NGX_OK` on success is fine.
  `ngx_http_core_access_phase()` advances to the next handler under
  the default `satisfy all` and only short-circuits under
  `satisfy any`, which is the documented meaning of that directive.

## Dependencies

- nginx core headers and `ngx_http.h` (`ngx_http_module_t`,
  `ngx_http_handler_pt`, `ngx_http_core_main_conf_t`, ...).

## Integrating as a submodule

From a host nginx module:

```sh
git submodule add -b <branch> <url> nxe-phase
```

In the host module's `config`:

```sh
nxe_phase_dir="$ngx_addon_dir/nxe-phase"
nxe_phase_tag="<unique token, e.g. your module's name>"

if [ ! -f "$nxe_phase_dir/config.ngx" ]; then
    echo "$0: error: $nxe_phase_dir/config.ngx not found" >&2
    exit 1
fi

. "$nxe_phase_dir/config.ngx"

ngx_module_deps="... $nxe_phase_module_deps"
ngx_module_incs="... $nxe_phase_module_incs"
ngx_module_srcs="... $nxe_phase_module_srcs"
ngx_module_libs="... $nxe_phase_module_libs"
```

`nxe_phase_tag` must be unique per consuming module: nxe-phase is
vendored source, so every consumer compiles its own copy of
`nxe_phase.c`, and the tag becomes part of the compiled `ngx_module_t`
symbol name (`nxe_phase_order_module_$nxe_phase_tag`) so several
copies can coexist in one nginx binary without a link-time collision.

Unlike nxe-json/nxe-cedar/nxe-jwx, nxe-phase compiles its own
`ngx_module_t`. `$nxe_phase_module_name` MUST be appended to the
parent's `$ngx_module_name` string as the *second or later* token —
`auto/module` uses the first token as the built `.so`'s file name:

```sh
ngx_module_name="ngx_http_my_module $nxe_phase_module_name"
```

## Building and testing

nxe-phase carries two independent test suites:

- `tests/unit/` — C unit tests for `nxe_phase_sort_entries()`, the
  pure function that decides ordering. Runs against malloc-backed
  nginx stubs, no nginx source tree required.

  ```sh
  cd tests/unit
  make test                                # default build + run
  make test-asan                           # AddressSanitizer
  make test-cov                            # gcov summary over src/
  NXE_PHASE_TEST_VERBOSE=1 make test       # show stub log output
  ```

- `tests/prove/` — [Test::Nginx](https://metacpan.org/pod/Test::Nginx)
  integration tests. Two dummy modules (`dummy_a`, `dummy_b`) register
  handlers in the same phase at different priorities and are loaded
  via `TEST_NGINX_LOAD_MODULES` in both orders, proving that response
  order follows priority and not `load_module` order. Requires a
  built nginx binary and the dummy `.so` files (base nginx built once,
  `dummy_a`/`dummy_b` each configured and built separately); see
  `.github/workflows/test.yaml` for the full setup.

  ```sh
  cd tests/prove
  TEST_NGINX_LOAD_MODULES="/path/to/nxe_phase_test_a_module.so /path/to/nxe_phase_test_b_module.so" \
    TEST_NGINX_BINARY=/path/to/nginx \
    TEST_NGINX_CONF_DIR="$PWD/conf" \
    TEST_NGINX_DATA_DIR="$PWD/conf" \
    prove -r .
  ```

## License

MIT. See [`LICENSE`](LICENSE).

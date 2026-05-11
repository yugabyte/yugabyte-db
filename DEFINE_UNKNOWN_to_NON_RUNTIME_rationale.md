# Conversion of DEFINE_UNKNOWN flags to DEFINE_NON_RUNTIME

## Background

`DEFINE_UNKNOWN_*` is a placeholder macro for flags that have not yet been classified
as runtime or non-runtime (see `src/yb/util/flags/flag_tags.h`):

```
// Unknown flags. !!Not to be used!!
// Older flags need to be reviewed in order to determine if they are runtime or non-runtime.
```

A flag is **non-runtime** when changing it after process start has no effect because the
value is consumed once during initialization (constructor, server bring-up, registration,
`Init()` path, `setrlimit` call, child-process exec argv, etc.) and never re-read.

A flag is **runtime** when its current value is read on every relevant operation, so that
a `SET FLAG` over RPC takes effect on subsequent work without restarting the process.

The flags below were re-classified as `DEFINE_NON_RUNTIME_*` after auditing every
`FLAGS_<name>` use site. Anything ambiguous (e.g. read per-RPC or per-session) was left
as `DEFINE_UNKNOWN_*` for a future, more careful pass.

---

## src/yb/server/webserver_options.cc

All `webserver_*` flags are read exactly once in `WebserverOptions::WebserverOptions()`
(file lines 120-143) to populate fields on the options struct that is then passed to
`Webserver`. The webserver listens on a fixed port and uses a fixed TLS / auth config
that is established at server startup. Mutating these flags later does not re-bind the
socket, re-load the certificate, or resize the worker thread pool.

| Flag | Rationale |
|------|-----------|
| `webserver_interface` | Copied into `WebserverOptions::bind_interface` at construction; binding happens once. |
| `webserver_ca_certificate_file` | TLS material loaded into `SSL_CTX` at startup; not refreshed afterwards. |
| `webserver_certificate_file` | Same as above. |
| `webserver_private_key_file` | Same as above. |
| `webserver_private_key_password` | Read once when decoding the private key. |
| `webserver_authentication_domain` | Stored on `WebserverOptions::authentication_domain`; used only when auth is configured at start. |
| `webserver_password_file` | `.htpasswd` location consumed at startup. |
| `webserver_num_worker_threads` | Sizes the squeasel worker pool created once. |
| `webserver_port` | Bind port; the listening socket is created once. |

## src/yb/server/server_base_options.cc

`ServerBaseOptions::ServerBaseOptions()` reads each flag once into a member of the options
struct. The base server then uses those copies for the rest of its lifetime
(`master_discovery_timeout_ms` was intentionally **not** converted because
`server::ResolveMasterAddresses()` reads the flag every call and that path can be invoked
post-startup during master-config refresh).

| Flag | Rationale |
|------|-----------|
| `placement_uuid` | Copied to `ServerBaseOptions::placement_uuid` at construction; used to register the server with the master once. |
| `server_dump_info_path` | Used by `ServerBase::Start()` to write the one-shot startup dump. |
| `server_dump_info_format` | Same one-shot dump path; format is fixed for that single dump. |
| `metrics_log_interval_ms` | Captured into the options struct; the metrics-log thread is started once with that cadence. |
| `server_broadcast_addresses` | Parsed once into `broadcast_addresses` during `ServerBaseOptions` construction. |

## src/yb/tserver/tablet_server.cc

The TS service-queue-length flags are arguments to `RegisterService()` calls in
`TabletServer::RegisterServices()` (lines 767-815) — service registration is a startup-only
operation, so changing the flag later cannot resize the queue.

| Flag | Rationale |
|------|-----------|
| `tablet_server_svc_queue_length` | Passed to `RegisterService(...)` at startup. |
| `ts_admin_svc_queue_length` | Same. |
| `ts_consensus_svc_queue_length` | Same. |
| `ts_remote_bootstrap_svc_queue_length` | Same. |
| `pg_client_svc_queue_length` | Same. |
| `ts_backup_svc_queue_length` | Same. |
| `xcluster_svc_queue_length` | Same; passed to the cdc service registration. |
| `enable_direct_local_tablet_server_call` | Read in `SetupAsyncClientInit()` exactly once when constructing the local proxy. |
| `redis_proxy_bind_address` | Used in `tablet_server_main_impl.cc` to fill the `RedisServer` rpc options before launching the proxy. |
| `redis_proxy_webserver_port` | Same; webserver bind happens once. |
| `cql_proxy_bind_address` | Same pattern for CQLServer. |
| `cql_proxy_webserver_port` | Same. |
| `inbound_rpc_memory_limit` | Read in the `TabletServer`/`Master` constructor to build the `YBInboundConnectionContext` factory. The factory is set on the messenger at startup. |
| `tserver_enable_metrics_snapshotter` | Decides whether the snapshotter is constructed during `Init()`; later reads at shutdown only check whether a snapshotter exists. Toggling at runtime cannot create or destroy it. |
| `get_universe_key_registry_backoff_increment_ms` | Read inside `TabletServer::Init()` to compute the universe-key fetch backoff schedule; `Init()` runs once. |
| `get_universe_key_registry_max_backoff_sec` | Same. |

(`tserver_yb_client_default_timeout_ms` was deliberately not converted: it is read on every
client session/lock-manager call site, so a runtime change still affects subsequent
operations.)

## src/yb/master/master.cc

Mirror of the tserver case — the queue-length flags are arguments to `RegisterService()`
during `Master::RegisterServices()` (lines 302-335).

| Flag | Rationale |
|------|-----------|
| `master_tserver_svc_queue_length` | Used at `RegisterService` time only. |
| `master_svc_queue_length` | Same; reused for the eight master-side services registered at startup. |
| `master_consensus_svc_queue_length` | Same. |
| `master_remote_bootstrap_svc_queue_length` | Same. |

(`master_yb_client_default_timeout_ms` was left as `UNKNOWN`: it is read on each
`client.NewSession(...)` call.)

## src/yb/yql/pgwrapper/pg_wrapper.cc

These flags configure the postgres child process. They are inspected in
`PgWrapper::Start()` / `PgSupervisor::Start()` to build the postmaster's argv and
environment **before** `exec()`. Once postmaster is running, the `FLAGS_*` value in the
tserver process no longer affects postgres — it would take a postmaster restart.

| Flag | Rationale |
|------|-----------|
| `postmaster_cgroup` | Passed via `YB_PG_INITIAL_CGROUP` env var; the cgroup is joined during postmaster startup. |
| `pg_transactions_enabled` | Forwarded via `YB_PG_TRANSACTIONS_ENABLED` env var consumed at postmaster boot. |
| `pg_verbose_error_log` | Translated into `log_error_verbosity` argv before exec. |
| `pgsql_proxy_webserver_port` | Forms the `yb_pg_metrics.port` argv argument before exec. |
| `pg_mem_tracker_tcmalloc_gc_release_bytes` | Forms an argv override for postgres mem-tracker config. |
| `pg_stat_statements_enabled` | Decides whether `pg_stat_statements` is added to `shared_preload_libraries` argv. |

## src/yb/util/ulimit_util.cc

`UlimitUtil::InitUlimits()` iterates the static `kRlimitsToInit` map (which holds
const-references to the flag values) and calls `setrlimit()` once. There is exactly one
call to `InitUlimits()` per process — in `master_main.cc:141` and
`tablet_server_main_impl.cc:310`. After process start the kernel-side rlimits are fixed;
flipping the flag has no effect.

| Flag | Rationale |
|------|-----------|
| `rlimit_data` | Consumed once by `setrlimit(RLIMIT_DATA, ...)` during InitUlimits. |
| `rlimit_nofile` (both Apple/Linux variants) | Same, for `RLIMIT_NOFILE`. |
| `rlimit_fsize` | `setrlimit(RLIMIT_FSIZE, ...)` once. |
| `rlimit_memlock` | `setrlimit(RLIMIT_MEMLOCK, ...)` once. |
| `rlimit_as` | `setrlimit(RLIMIT_AS, ...)` once. |
| `rlimit_stack` | `setrlimit(RLIMIT_STACK, ...)` once. |
| `rlimit_cpu` | `setrlimit(RLIMIT_CPU, ...)` once. |
| `rlimit_nproc` | `setrlimit(RLIMIT_NPROC, ...)` once. |

## src/yb/yql/pggate/pggate_flags.cc

| Flag | Rationale |
|------|-----------|
| `pggate_ybclient_reactor_threads` | Consumed when constructing the pggate `YBClient` in `PgApiImpl::PgApiImpl()` (`pggate.cc:758`); the reactor pool is sized once. |
| `pggate_master_addresses` | Forwarded from tserver to postmaster via `FLAGS_pggate_master_addresses` env var when launching pg (`pg_wrapper.cc:1386`); the value is captured at child exec. |

(The remaining `ysql_*` and `ysql_beta_*` flags in this file were left as `UNKNOWN` — most
of them are checked by the YSQL tier on every statement / planner pass, so they may
legitimately be runtime.)

---

## Skipped / not yet converted

The following common-looking candidates were intentionally **left** as `DEFINE_UNKNOWN_*`
because their use sites are read per-operation, which means runtime mutation today does
take effect:

- `master_discovery_timeout_ms` — re-read by `ResolveMasterAddresses()` on every retry loop.
- `master_yb_client_default_timeout_ms`, `tserver_yb_client_default_timeout_ms` — read per
  `client.NewSession()` and per-lock acquire.
- `allow_insecure_connections`, `verify_client_endpoint`, `verify_server_endpoint`,
  `cipher_list`, `ciphersuites`, `ssl_protocols`, `dump_certificate_entries` (in
  `secure_stream.cc`) — read per TLS handshake / per cert-validation event.
- `ycql_ldap_*` (in `cql_processor.cc`) — read per LDAP authentication.

Those should be classified separately, possibly as runtime, after confirming the intended
operator workflow.

---

## Total

50 `DEFINE_UNKNOWN_*` declarations across 7 files were converted to `DEFINE_NON_RUNTIME_*`.

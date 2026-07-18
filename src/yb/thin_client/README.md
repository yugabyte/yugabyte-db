# yb_thin_client — a thin, Perform-based tserver client

`yb_thin_client` is a small C-ABI library that talks directly to a
`yb-tserver`'s `yb.tserver.PgClientService.Perform` RPC via
`PgClientServiceProxy`. It is meant to be built here, shipped as a shared
object, and linked by a foreign (e.g. non-C++) caller that needs YugabyteDB's
hot read/write paths without embedding a full client.

## Design

- **Server-side routing only.** Each op carries its own partition routing info
  (`hash_code` + `partition_key`, computed from the hash columns), and the
  tserver routes it to the right tablet — exactly as it does for pggate. There
  is deliberately **no** `YBClient` / `YBSession` / `Batcher` / metacache /
  `TabletInvoker` on the client side.
- **No master dependency.** The client connects to tserver RPC endpoints only.
- **Thin C ABI.** Everything YugabyteDB-specific lives behind
  `yb_thin_client.h`. The caller marshals POD structs, bridges the async
  callbacks to its runtime, and maps status codes — it does not build requests,
  manage sessions, compute partition keys, or frame RPCs.
- **Non-goals:** no distributed transactions (ops are single-shard /
  non-transactional), no DDL, no catalog-version checks, no shared-memory
  (`SharedExchange`) Perform — it uses the plain RPC `PerformAsync` because the
  caller is typically on a different host than the tserver.

## API (see `yb_thin_client.h` for the authoritative contract)

- `ybthin_client_create` / `ybthin_client_destroy` — connect and open a pool of
  `PgClientService` sessions (Heartbeat) across one or more connections, with a
  keepalive; optional TLS (server-auth or mTLS) via `SecureContext`. A single
  session serializes its Performs server-side, so concurrency comes from having
  many sessions; `ybthin_pool_opts` sizes the pool (read/write sessions packed
  onto connections). Reads round-robin the read sessions; upserts round-robin the
  write sessions.
- `ybthin_table_open` / `ybthin_table_close` / `ybthin_columns_free` — resolve a
  table by `(db_oid, table_oid)` and fetch its schema (columns in schema order:
  hash, then range, then value). Also the startup health check.
- `ybthin_read_page_async` — one page of a read; delivers the `pg_doc_data` row
  sidecar + opaque `paging_state` + `used_read_time_ht`.
- `ybthin_upsert_batch_async` — N `PGSQL_UPSERT` rows as the ops of one Perform.
- `ybthin_read_result_free` / `ybthin_string_free` — free shim-owned buffers.

Status codes: `OK / INVALID / NETWORK / TRY_AGAIN / READ_RESTART / SCHEMA /
OTHER`. Callbacks may fire on a YB reactor thread, so they must be cheap and
non-blocking (signal a channel and return).

## Versioning / backward compatibility

This library is **upgraded before the tserver**, so a given build must keep
working against an **older** tserver than it was compiled against. Keep the wire
usage backward-compatible: only send request fields / rely on RPCs and semantics
the oldest supported tserver understands, and tolerate responses that lack fields
a newer server would set. The C ABI is additive-only (new functions; new struct
fields / enum values appended at the end; never renumber, reorder, or remove).

## Build

```
./yb_build.sh release --target yb_thin_client
```

produces `build/<build-root>/lib/libyb_thin_client.so`. The transitive `.so`
closure it links (the deploy set) is the `PgClientServiceProxy` closure
(`pg_client_proto`, `yb_common`, `yb_dockv`, `yrpc`, `yb_util` + transitive proto
libs) and deliberately excludes the engine libs (`yb_client`, `master`,
`consensus`, `docdb`, `tablet`). The exact, versioned list is documented at the
top of `yb_thin_client.cc`; regenerate it with `ldd` against the built `.so`.

Deploy model: ship the `.so` with its runtime closure (or a single self-contained
object) and **version-pair it with the tserver** it talks to.

## Test

`yb_thin_client-itest` drives the whole C ABI against a pgwrapper mini cluster:

```
./yb_build.sh release --cxx-test yb_thin_client-itest
```

- `PgThinClientTest.OpenUpsertReadPaged` — open table (schema asserted), upsert
  rows (cross-checked via SQL), and page a bounded scan.
- `PgThinClientTlsTest.ClientCreateOverTls` — under node-to-node +
  client-to-server encryption, a plaintext client is rejected by the TLS-only
  endpoint and a TLS `client_create` (test CA) succeeds.

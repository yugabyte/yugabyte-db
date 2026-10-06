# YSQL authentication catalog follower reads

Implemented behind an experimental, default-false routing flag. See
[local reproduction](ysql-auth-follower-reads-local.md) for validation and commands.
Linux, debug, sanitizer, mixed-version, and performance validation remain outstanding.

## Restore interaction

PITR and system-catalog restore remain available. Restore is a non-MVCC Raft
operation: it does not hold back follower safe time, and fast PITR changes which
history a fixed read time returns. Every operation at or before T reaches a
follower before that follower's safe time can reach T. A follower therefore serves
a marked read only when its safe time is at least T and its applied index covers
its received index; otherwise the client retries the leader at the same T. Bursts
of sys-catalog writes can make followers reject more often and add leader reads.

A restore that completes before an attempt acquires T is visible to that attempt
on every replica, including `template1` restores that roll back passwords, role
membership, and role settings. An attempt that overlaps a restore can fail or use
pre-restore catalog data. Catalog-version invalidation after the restore refreshes
the session, as it does for leader reads.

## Fresh authentication snapshot and routing

`ysql_enable_auth_catalog_follower_reads` defaults to false in masters, tservers,
and PostgreSQL/pggate. Enable it only after every peer runs compatible binaries:
masters first, then restart participating tservers/postmasters with the forwarded
flag.

For an eligible uncached authentication attempt:

1. PostgreSQL resets prefetch state, then asks the master leader for one fresh
   snapshot through `GetYsqlAuthCatalogReadTime`. The leader checks its routing
   flag, incorporates propagated hybrid time,
   selects `T = MaxGlobalNow()`, waits for lease-backed safe time at least T, and
   rechecks the leader term. A lagging safe-time value alone is not a fresh read
   boundary.
2. Pggate retains `ReadHybridTime::SingleTime(T)` through authentication, database
   CONNECT checks, and role settings, across individual prefetch lifetimes. The
   tserver validates the uncached, nontransactional catalog-session envelope and
   rejects writes, row locking, transactional state, and conflicting paging times.
3. Only allowlisted full catalog scans and their matching indexes are tagged for
   follower routing: current-version `template1` IDs for `pg_authid`,
   `pg_database`, `pg_auth_members`, `pg_db_role_setting`,
   `pg_yb_catalog_version`, and `pg_yb_logical_client_version`. Predicates, server
   expressions, sampling, backfill, vector requests, and unrelated relations do
   not pass the follower validator. Other permitted pure catalog reads stay
   STRONG at the same T; they do not broaden follower admission.
4. The client prefers a known nonleader, distributing requests among equally
   local followers. A serving master independently validates the strict tagged
   envelope, initialization, non-shell state, safe time at least T, and (on a
   follower) applied operations. A tagged `CONSISTENT_PREFIX` read also requires its
   routing flag. Untagged master reads remain leader-only.
5. Follower safe-time wait is bounded by 1000 ms and half the remaining RPC
   deadline. The client bounds the follower RPC by 2000 ms and half its remaining
   budget, including unreachable followers. These are internal limits, not flags.
   Retry through leader selection explicitly switches to STRONG and waits under
   the leader lease at the same T. Neither fallback nor paging chooses a new
   snapshot. Conflicting restart/response read times fail rather than silently
   advancing T.

The snapshot ends explicitly after CONNECT checks and `pg_db_role_setting`, before
ordinary session initialization can execute SQL. Catalog-version invalidation is
deferred while T is pinned and resumes afterward. Snapshot acquisition failure is
an authentication-startup error, not permission to use an older snapshot.

### Bounded acquisition and latency

`GetYsqlAuthCatalogReadTime` runs on dedicated, on-demand worker pools rather than
holding general RPC workers during the clock/lease wait. Admission includes queued
and running tasks; excess requests receive `ServiceUnavailable`. Queue plus execution
is bounded by the RPC deadline and an internal 5000 ms budget. Expiration returns
`TimedOut`; shutdown rejects queued tasks and joins running tasks.

| Internal limit | Master | Tserver |
| --- | --- | --- |
| Workers | 32 | 16 |
| Running plus queued tasks | 128 | 64 |
| Queue plus execution budget | 5000 ms | 5000 ms |

These limits bound resource use, not guaranteed capacity. They are not tuning
flags; deterministic tests configure them through sync points.
Selecting `MaxGlobalNow()` can add approximately the clock-skew bound to each
eligible login (500 ms with the default wall clock). Freshness is not traded away
to avoid that wait. Measure connection latency, overload, and leader row/byte work
before enabling this experimentally; no throughput improvement is established.

## Excluded paths and cache behavior

The PostgreSQL entry point is restricted to direct TCP client backends. Unix
sockets, connection-manager authentication/physical/control/passthrough backends,
internal/background workers, autovacuum, initdb, and binary-upgrade paths retain
their existing behavior. Login profiles are excluded whenever
`ysql_enable_profile=true`, even before profile catalogs exist, because
authentication can write profile state.

Response-cache selection takes precedence over follower routing. Every attempt
that selects the authentication response cache, including misses and retries,
stays on the existing cache/leader path. Shared-cache freshness semantics are not
changed by this feature. To exercise follower routing locally, explicitly set
`ysql_enable_read_request_cache_for_connection_auth=false`; do not disable global
`ysql_enable_read_request_caching`. Forwarded PostgreSQL gflags are captured when
the postmaster starts, so changing only the live tserver flag is insufficient.

Later phase-3 prefetches within an opted-in attempt also bypass the shared response
cache, because its data may come from another snapshot. Per-attempt PostgreSQL
prefetch remains enabled. This can increase leader work for non-allowlisted catalogs,
so include warm/cold relcache and broad-preload configurations in performance tests.

## Observability and validation

Master metrics distinguish `ysql_auth_catalog_snapshot_acquisitions`,
`ysql_auth_catalog_follower_reads`, and `ysql_auth_catalog_leader_reads`. Read
counters increment after envelope, safe-time, and applied-operation checks,
classified by the actual serving replica's role. They count admissions, not
completed storage reads or successful logins. A wrong password can still cause a
read admission.

The metric prefixes `master_ysql_auth_snapshot_` and `tserver_ysql_auth_snapshot_`
expose `task_limit_rejections`, `deadline_expirations`, and `outstanding_tasks`.
Monitor both layers: a full tserver queue can reject logins before the master sees
any acquisition. Read-admission counters alone do not show this overload or prove
reduced leader load.

Read-path tests must cover fresh role/password changes, follower admissions,
fixed-T leader fallback, malformed envelopes, lag/timeout handling, PITR restore
of shared catalogs with lagging and catching-up followers, cache/profile
exclusions, direct-TCP scope, and default-off behavior. The integration target is
`pg_auth_follower_reads-test`; commands are in the local reproduction document.
Functional tests do not establish a performance improvement.

## Upgrade/Rollback safety

Keep routing off until every master, and every participating tserver/PostgreSQL
process, runs compatible binaries. No state is persisted and no capability is
promoted; disabling the flag returns authentication to leader-only reads.
Mixed-version behavior has not been tested.

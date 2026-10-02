# YSQL authentication catalog follower reads

Implemented behind an experimental, default-false routing flag. See
[local reproduction](ysql-auth-follower-reads-local.md) for validation and commands.
Linux, debug, sanitizer, mixed-version, and performance validation remain outstanding.

## Permanent operational restriction

**The experimental, non-runtime master flag `--disable_pitr=true` permanently
excludes PITR. It is new in this stack, not an existing released setting.
Turning follower-read routing off does not undo the mode.**

- New universes can opt in at creation. Eligible existing universes can opt in
  during a coordinated master restart; rolling and live activation are unsupported.
- Servers reject PITR schedules and system-catalog restore across the universe,
  including YCQL: the masters share one physical system-catalog tablet.
- Changing or omitting the startup flag, restarting, or leader failover cannot
  remove the persisted mode.
- Re-enabling PITR in place is unsupported. Do not choose this mode if the
  universe will need PITR later.
- Ordinary backup snapshots and data-only restores remain available. Workflows
  that require PITR schedules or system-catalog restore are excluded.

Read the persisted mode through the existing `GetMasterClusterConfig` RPC or:

```sh
yb-admin --master_addresses "$MASTERS" get_universe_config
```

The cluster config reports `pitr_disabled` (`pitrDisabled` in JSON). There is no
reservation/status RPC or CLI, acknowledgement, capability AutoFlag or promotion,
or pending admission state.

## Why exclude restore

Normal MVCC writes preserve historical snapshots. System-catalog restore can
change the historical data visible to a read. Supporting both features requires
restore-overlap checks, durable read boundaries, recovery rules, and cross-replica
restore validation. This rollout instead prohibits the conflicting operation
before any follower reads are enabled.

## Activation and persistence

New-universe bootstrap stores `SysClusterConfigEntryPB.pitr_disabled` in the
replicated system catalog. An existing universe preserves its configuration and
identifiers while committing the same one-way mode change.

### Existing universes

Install mode-aware binaries on every master, with routing off. Inspect actual
schedules, snapshots, and restorations before planning the control-plane outage.
PITR behavior flags such as `enable_fast_pitr` do not establish eligibility.

Stop all masters before restarting any with `--disable_pitr=true`. Before leader
readiness, recovery must complete and eligibility must hold:

- No schedule metadata, including deleted schedules awaiting cleanup.
- No retained schedule snapshots, even if the schedule has already disappeared.
- No unfinished or incompletely recorded restoration. An aggregate FAILED or
  RESTORED status alone is not sufficient evidence of finalization.

Ordinary snapshots and fully finalized restoration history are allowed. Past PITR
use does not permanently disqualify a universe. Activation does not delete or
abort blocking state automatically.

A blocker refuses startup and identifies the object. Restart without the request
to recover service and explicitly finish or clean up the blocking work. A crash
or write error can occur after commitment: after recovery, inspect the persisted
mode rather than inferring it from the error. Only enable routing after verifying
`pitrDisabled=true`. Removing the request does not undo a committed transition.

### Replica and recovery rules

Each process captures startup intent before RPC services start; a forced runtime
flag change cannot request activation. A requesting process cannot become ready
before the eligibility check and replicated write succeed. This is not a live
check/write protocol on a serving leader and has no pending admission state.

Every master replica loads and applies the durable mode. Administrative config
updates cannot change it or clear it by omitting the field. Already-disabled
universes skip eligibility rechecking so ordinary restore recovery can proceed.

An internal marker identifies initial-snapshot template metadata. Initdb recovery
preserves a committed mode rather than resetting it from the template or startup
flag. Use a matching initial snapshot; run `yb_build.sh ... reinitdb` in reused
build directories. Direct system-catalog edits are not a supported conversion.

## Fresh authentication snapshot and routing

`ysql_enable_auth_catalog_follower_reads` defaults to false in masters, tservers,
and PostgreSQL/pggate. The persisted mode does not enable routing. Enable this
flag only after verifying persisted PITR-disabled mode and compatible binaries on
all peers; enabling it earlier can fail eligible authentication startup. Enable
masters first, then restart participating tservers/postmasters with the forwarded
flag.

For an eligible uncached authentication attempt:

1. PostgreSQL resets prefetch state, then asks the master leader for one fresh
   snapshot through `GetYsqlAuthCatalogReadTime`. The leader checks its routing
   flag and persisted PITR-disabled mode, incorporates propagated hybrid time,
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
   envelope, initialization, non-shell state, locally applied PITR-disabled mode,
   and safe time at least T. A tagged `CONSISTENT_PREFIX` read also requires its
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
counters increment after envelope, PITR-disabled mode, and safe-time checks,
classified by the actual serving replica's role. They count admissions, not
completed storage reads or successful logins. A wrong password can still cause a
read admission.

The metric prefixes `master_ysql_auth_snapshot_` and `tserver_ysql_auth_snapshot_`
expose `task_limit_rejections`, `deadline_expirations`, and `outstanding_tasks`.
Monitor both layers: a full tserver queue can reject logins before the master sees
any acquisition. Read-admission counters alone do not show this overload or prove
reduced leader load.

Validation must cover new-universe selection, coordinated restart of eligible
existing universes, rejection of retained or unfinished PITR state, runtime flag
mutation, admission before commit, write failure and crash recovery, and unchanged
configuration and user data. Check mode persistence through replay/restart/failover
and ordinary restore recovery. The mode target is `pitr_disabled-test`. Read-path tests must cover
fresh role/password changes, follower admissions, fixed-T leader fallback,
malformed envelopes, lag/timeout handling, cache/profile exclusions, direct-TCP
scope, and default-off behavior. The integration target is
`pg_auth_follower_reads-test`; commands are in the local reproduction document.
Functional tests do not establish a performance improvement.

## Upgrade/Rollback safety

Install compatible binaries on every master before requesting permanent PITR
exclusion, and on participating tserver/PostgreSQL processes before enabling
routing. No capability promotion is involved. Do not activate with mixed versions.

**Do not downgrade PITR-disabled data to binaries unaware of this mode.** They
could admit PITR. Disabling routing or changing the startup flag is not a
conversion or rollback procedure.

Old experimental reservation universes are unsupported: create a new universe
with new data directories; do not silently convert or reuse their metadata.

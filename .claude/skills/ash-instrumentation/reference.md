# ASH instrumentation — code reference

File/line citations are against `master` and **will drift across commits** — treat every number
(including the `(~N)` approximate-line markers below) as a hint only, and `grep` the named symbol to
locate the current line. All paths are relative to the repo root.

## Macros — `src/yb/ash/wait_state.h` (~lines 36-67)

| Macro | Meaning |
|---|---|
| `SET_WAIT_STATUS(code)` | Set current thread's wait state code immediately, no revert. |
| `SET_WAIT_STATUS_TO(ptr, code)` | Set `ptr`'s code (no-op if `ptr` is null). |
| `SET_WAIT_STATUS_TO_CODE(ptr, code)` | Same, but `code` is a full `WaitStateCode` value. |
| `SET_WAIT_STATUS_FROM_SNAPSHOT(snapshot)` | Restore from a saved snapshot. |
| `SCOPED_WAIT_STATUS(code)` | RAII; sets on entry, reverts to previous code at scope exit. |
| `SCOPED_WAIT_STATUS_FOR_QUERY_ID(fixed_query_id, if_match, otherwise)` | Scoped; picks `if_match` when current query id == `fixed_query_id`, else `otherwise`. |
| `ADOPT_WAIT_STATE(ptr)` | RAII (`ScopedAdoptWaitState`); adopt `ptr`'s wait state onto this thread, restore on exit. |
| `ASH_ENABLE_CONCURRENT_UPDATES()` / `_FOR(ptr)` | Allow concurrent updates to a wait state; required for async handoff to avoid DFATAL in `~ScopedWaitStatus`. |

`code` arguments are written **without** the `k` prefix; the macros prepend
`yb::ash::WaitStateCode::k` via `BOOST_PP_CAT`.

## Enums — `src/yb/ash/wait_state.h`

Wait event code layout (32-bit): `<4-bit Component> <4-bit Class> <8-bit reserved> <16-bit Event>`.

- `Component` (~106): `kYSQL`, `kYCQL`, `kTServer`, `kMaster`. Do not reorder.
- `Class` (~114): `kTServerWait`, `kYCQLQuery`, `kClient`, `kRpc`, `kConsensus`, `kTabletWait`,
  `kRocksDB`, `kCommon`. Do not reorder.
- `WaitStateCode` (~140): all wait events, grouped by `YB_ASH_MAKE_EVENT(<Class>)`. The first member
  of each class group carries the `((kName, YB_ASH_MAKE_EVENT(Class)))` marker; the rest are plain.
- `FixedQueryId` (~230): synthetic query ids for background tasks (`kQueryIdForLogAppender`,
  `kQueryIdForFlush`, `kQueryIdForCompaction`, `kQueryIdForRaftUpdateConsensus`, ...).
- `WaitStateType` (~246): `kCpu`, `kDiskIO`, `kRPCWait`, `kWaitOnCondition`, `kLock`. The coarse
  resource bucket; not encoded in the code bits, derived via `GetWaitStateType`.
- `PggateRPC` (~256): instrumented pggate sync RPCs (used for `kWaitingOnTServer` aux info).

## Mapping & description — `src/yb/ash/wait_state.cc`

- `GetWaitStateType(WaitStateCode)` (~590): switch returning the `WaitStateType`. Add a `case` for
  each new code.
- `GetWaitStateDescription(WaitStateCode)` (~104): switch returning the human-readable string shown
  in `yb_wait_event_desc`. No `default` — every code must have a case.
- `GetWaitStateAuxDescription(WaitStateCode)` (~680): **third** switch, just below `GetWaitStateType`.
  Returns what `wait_event_aux` holds for the code. Also no `default`, so a missing case is a build
  error. Tablet-bearing codes go in the `"This contains tablet ID."` group; others in the `""` group.
- `GetWaitStatesDescription()` (~456): builds the full `(code, description)` list consumed by the PG
  view layer.

## Tablet id helpers — `src/yb/ash/wait_state.h`

- `WaitStateInfo::UpdateCurrentTabletId(TabletIdView)` (~546): static; sets on the current thread's
  wait state. Usages: `service_util.cc:278`, `cdc_service.cc:1643`, `backup_service.cc:75`.
- `WaitStateInfo::UpdateTabletId(TabletIdView)` (~545): on an explicit `WaitStateInfo` ptr. Usages:
  `transaction_participant.cc:222`, `xcluster_poller.cc:844`.

## Background trackers — `src/yb/ash/wait_state.cc` (~805-827)

`WaitStateTracker` (declared in `wait_state.h`) has `Track()`, `Untrack()`, `GetWaitStates()`.
Singleton accessors:

- `FlushAndCompactionWaitStatesTracker()` — RocksDB flush/compaction.
- `RaftLogWaitStatesTracker()` — Raft log appender/sync.
- `SharedMemoryPgPerformTracker()` — shared-memory PG Perform.
- `SharedMemoryPgAcquireObjectLockTracker()` — object lock acquisition.
- `XClusterPollerTracker()` — xCluster pollers.
- `MinRunningHybridTimeTracker()` — min running hybrid time.

## Collector — `src/yb/tserver/pg_client_service.cc`

- `ActiveSessionHistory(req, resp, context)` (~2507): top-level sampler. Pulls RPC wait states via
  `GetRpcsWaitStates(...)` and non-RPC ones via `AddWaitStatesToResponse(<Tracker>(), ...)`.
- `GetRpcsWaitStates(...)` (~2453): iterates connections through `messenger->DumpRunningRpcs`
  (like `DumpRpcCalls`) with `get_wait_state=true`.
- `AddWaitStatesToResponse(tracker, ...)` (~2484): iterates `tracker.GetWaitStates()`, skips
  `kIdle`. **A new tracker must get a call here to be sampled.**
- `ShouldIgnoreCall(req, call)` (~2398): per-RPC filter applied by `PopulateWaitStates`/
  `GetRpcsWaitStates`. Drops calls with no wait state, `kIdle`, and — when
  `req.ignore_ash_and_perform_calls()` is set — methods `ActiveSessionHistory`, `AcquireAdvisoryLock`,
  and `Perform`. `Perform` has one allowlist carve-out: `kXCluster_WaitForSafeTime`. The real sampler
  always sets the flag (`pg_client.cc:1756`), so a service-layer wait event on the `Perform` path must
  add its code to this `Perform` allowlist or it is dropped in production.

## RPC metadata options — `src/yb/rpc/service.proto` (~14-32)

```proto
extend google.protobuf.ServiceOptions {
  bool service_send_metadata = 50012;  // send ASH metadata with all RPCs of the service
}
extend google.protobuf.MethodOptions {
  bool send_metadata    = 50002;       // opt one RPC in
  bool exclude_metadata = 50003;       // opt one RPC out (when service flag is set)
}
```

Example usages in `src/yb/rpc/rtest.proto` (`AshTestService`, `CalculatorService`) and on real
services like `TabletServerService` (`Write`/`Read`) and `PgClientService`.

## Master pass-through (metadata threading) — `src/yb/master/`

Master is not sampled, but RPCs orchestrated through it must carry ASH metadata to the tserver. The
index-backfill path is the reference implementation (commit `fba4bbb378e`):

- `backfill_index.{cc,h}` — `BackfillTable`/`BackfillTablet` hold a captured `wait_state_`
  (`CreateIfAshIsEnabled` + `UpdateMetadata(current->metadata())` + `UpdateAuxInfo({.method=...})`);
  `ADOPT_WAIT_STATE(...)` in `LaunchBackfillOrAbort`, `SendRpcToAllowCompactionsToGCDeleteMarkers`,
  `GetSafeTimeForTablet::SendRequest/HandleResponse/UnregisterAsyncTaskCallback`,
  `BackfillChunk::SendRequest`.
- `async_rpc_tasks.{cc,h}` — `AsyncAlterTable` captures `wait_state_` in its ctors
  (`CurrentWaitState()` or fresh) and adopts it in `SendRequest`/`HandleResponse`.
- Proto opt-ins: `MasterDdl.BackfillIndex` (`master_ddl.proto`); `AlterSchema`, `GetSafeTime`,
  `BackfillIndex`, `BackfillDone` (`tserver_admin.proto`).

## Test — `src/yb/integration-tests/wait_states-itest.cc`

- `AshTestVerifyOccurrence` param test (`VerifyWaitStateEntered`) — one case per code.
- `INSTANTIATE_TEST_SUITE_P(WaitStateITest, AshTestVerifyOccurrence, ::testing::Values(... codes ...))`
  (~992) — **add the new code here.**
- Per-code workload/sleep switches (~110, ~718, ~950) — extend if a special condition is needed to
  reach the state (e.g. create-index, compaction, remote bootstrap, add-node).
- `ash::WaitStateInfo::TEST_EnteredSleep()` (~752) is how the test asserts the code was entered.

Run:

```bash
./yb_build.sh release --cxx-test wait_states-itest --gtest_filter "*<YourCode>*"
```

## Query-id test — `src/yb/yql/pgwrapper/pg_ash-test.cc`

`wait_states-itest` does not verify `query_id`; `pg_ash-test.cc` does, by querying
`yb_active_session_history`. Two reference patterns:

- `TestTServerMetadataSerializer` (~990) — user-driven path: fetch `queryid` from
  `pg_stat_statements`, assert `COUNT(*) ... WHERE query_id = $0` > 0.
- `TestMinRunningHybridTimeWaitEventHasTabletId` (~1013) — background task: assert against
  `std::to_underlying(ash::FixedQueryId::kQueryIdForMinRunningHybridTime)`.

```bash
./yb_build.sh release --cxx-test pg_ash-test --gtest_filter "*<YourTestName>*"
```

## FixedQueryId — `src/yb/ash/wait_state.h` (~230)

Synthetic query ids for background (non-RPC) tasks. Real usages to copy (grep `UpdateMetadata` or the
specific `kQueryIdFor...` in each file — line numbers drift): `flush_job.cc`, `compaction_job.cc`,
`raft_consensus.cc`, `log.cc`, `transaction_participant.cc`, `tablet_service.cc` (remote bootstrap),
`xcluster_poller.cc`, `async_snapshot_tasks.cc`. For a background path, reuse one of these or
add a new entry (next explicit number, no reordering) — this is a user decision (see SKILL.md).

## User-facing views

- `yb_active_session_history` — `src/yb/yql/pgwrapper/ysql_migrations/V45__19128__yb_ash.sql`,
  backed by `yb_active_session_history()` in `src/postgres/src/backend/utils/misc/yb_ash.c`.
- `yb_wait_event_desc` — `.../ysql_migrations/V55__19131__yb_wait_event_desc.sql`, backed by
  `GetWaitStateDescription`.

---
name: ash-instrumentation
description: >-
  Instrument YugabyteDB ASH (Active Session History) wait states: add a new
  WaitStateCode, choose the right macro for synchronous vs asynchronous/callback
  code paths, register background trackers, plumb ASH metadata through RPCs, and
  test with wait_states-itest. Use when adding or changing wait states, or when
  instrumenting a code path for ASH anywhere under src/yb.
---

# Instrumenting ASH wait states

Active Session History (ASH) samples, for every running RPC/task, the **wait state** it is currently
in: what resource it is blocked on. This skill is the current, code-accurate procedure for adding or
instrumenting wait states.

**Scope: TServer / DocDB C++ code only (`src/yb`).** These macros and the `WaitStateCode` enum are
for the TServer/DocDB layer. Do **not** use them in PostgreSQL backend code (`src/postgres`) — PG has
its own wait-event mechanism (`pgstat` wait events / `yb_ash.c`).

For the full file/line code map (enum tables, macro definitions, collector, proto options), read
`reference.md` in this skill directory.

## Mental model

- A wait state is the resource a task is **blocked** on, at a granularity that is meaningful to a
  developer/user — not fine-grained.
- Coarse on purpose: do **not** add wait states for spinlocks/mutexes. Instrument the places where
  you wait on a **condition variable, a callback, IO, network (outgoing RPC), or a row/txn lock**.
- A few CPU states exist but are kept coarse.
- Every `WaitStateCode` is assigned a coarse `WaitStateType` (`kCpu`, `kDiskIO`, `kRPCWait`,
  `kWaitOnCondition`, `kLock`) — tag it with the resource it spends the **most** time on.

## Decision tree: which macro?

All macros are in `src/yb/ash/wait_state.h` (header is included as `yb/ash/wait_state.h`).

| Situation | Use |
|---|---|
| Synchronous wait on the current thread; auto-revert at scope end | `SCOPED_WAIT_STATUS(Code)` |
| Set immediately, no scope/auto-revert (or set on a specific ptr) | `SET_WAIT_STATUS(Code)` / `SET_WAIT_STATUS_TO(ptr, Code)` |
| Pick between two events based on the current query id | `SCOPED_WAIT_STATUS_FOR_QUERY_ID(fixed_query_id, if_match, otherwise)` |
| Work handed off to another thread / threadpool / a callback | **Do NOT use SCOPED.** See async pattern below |

Note: pass the code **without** the `k` prefix — the macros prepend `yb::ash::WaitStateCode::k`
(e.g. `SCOPED_WAIT_STATUS(RocksDB_Flush)`).

### Synchronous (most common)

```cpp
void DoSyncWork() {
  SCOPED_WAIT_STATUS(MyClass_MyWait);   // entered here
  ... blocking work ...
}                                        // reverts to previous code on scope exit
```

### Asynchronous / callback (gets this wrong → DFATAL or missed samples)

When the work continues on a different thread or completes in a callback, the calling thread exits
the function immediately, so a scoped revert would clear the state too early. Instead:

1. In the **caller**, just before handing off:
   - `SET_WAIT_STATUS(MyClass_MyWait)` (or on the explicit wait-state ptr), and
   - `ASH_ENABLE_CONCURRENT_UPDATES()` — required because the state may now be updated from more
     than one thread. **Omitting this can DFATAL in `~ScopedWaitStatus`** and fail tests.
2. In the **worker thread / callback**:
   - `ADOPT_WAIT_STATE(ptr)` to adopt the RPC/task's wait state onto this thread, then
   - set the appropriate code (`SET_WAIT_STATUS` / `SCOPED_WAIT_STATUS`).

The ServicePool already adopts the inbound RPC's wait state for the initial handler thread, so you
only need ADOPT when *you* hand work to another threadpool/layer. `YBClient::FlushAsync` already
adopts correctly — if you only call FlushAsync you don't need to redo it.

Real examples (these files are large — grep for the macros instead of reading top to bottom):
- Caller handing off work:
  `grep -n 'ASH_ENABLE_CONCURRENT_UPDATES\|SET_WAIT_STATUS' src/yb/tablet/write_query.cc`
  — the `SET_WAIT_STATUS(OnCpu_Passive)` + `ASH_ENABLE_CONCURRENT_UPDATES()` pair just before handoff.
- Worker/callback picking it up:
  `grep -n 'ADOPT_WAIT_STATE' src/yb/tablet/operations/operation_driver.cc`
  — each entry point does `ADOPT_WAIT_STATE(wait_state())` then sets the code.

## Add a new wait state (checklist)

1. **Enum entry** — add the `WaitStateCode` under the correct `Class` group in
   `src/yb/ash/wait_state.h` (the `WaitStateCode` enum, grouped by `YB_ASH_MAKE_EVENT(<Class>)`
   markers). The first entry of a class carries the marker; subsequent ones just list the name.
2. **Type mapping** — add a `case` to `GetWaitStateType(...)` in `src/yb/ash/wait_state.cc`
   returning the right `WaitStateType`.
3. **Description** — add a `case` to `GetWaitStateDescription(...)` in `src/yb/ash/wait_state.cc`.
   This switch has no `default`, so a missing case is caught; the string surfaces in the
   `yb_wait_event_desc` view.
4. **Aux description** — add a `case` to `GetWaitStateAuxDescription(...)` in `src/yb/ash/wait_state.cc`
   (just below `GetWaitStateType`). This is a **third** switch that also has no `default`, so a
   missing case is a **compile error** (`-Werror=switch`) — easy to forget. Put codes that set a
   tablet id in the `"This contains tablet ID."` group; otherwise add them to the `return ""` group.
5. **Instrument the code path** — use the decision tree above at the actual wait site.
6. **Test (two parts)** — (a) add the code to `INSTANTIATE_TEST_SUITE_P(...)` in
   `src/yb/integration-tests/wait_states-itest.cc` to prove the state is entered (extend the per-code
   switches at lines ~110 / ~718 / ~950 if a special workload is needed); **and (b)** add a
   `pg_ash-test.cc` test asserting the ASH sample carries the correct `query_id` for the new path
   (see "Test it" below). Both are required.

## Set the tablet id wherever applicable

If the wait happens in the context of a specific tablet, set it on the wait state so it shows up in
`wait_event_aux` / the sample — it is one of the most useful fields for narrowing down a hotspot. Use:

- `ash::WaitStateInfo::UpdateCurrentTabletId(tablet_id)` — static; sets it on the current thread's
  wait state (the common case for RPC handlers; see `service_util.cc`, `cdc_service.cc`,
  `backup_service.cc`).
- `wait_state_ptr->UpdateTabletId(tablet_id)` — when you hold an explicit `WaitStateInfo` (background
  tasks; see `transaction_participant.cc`, `xcluster_poller.cc`).

Set it as early as the tablet is known. Both take a `TabletIdView`.

Set it **once per RPC/wait state**, at the outermost point where the tablet is known — you do not
need to re-set it at each API/call layer the request passes through. The value rides on the wait
state, so later layers inherit it. Note the tablet id is **not** part of the metadata sent across
RPCs (unlike `query_id`/`root_request_id`) — it lives only on the local wait state, so a downstream
RPC handler that needs it must set it again on its own side.

## Background tasks (non-RPC)

A lot of DocDB IO runs on background threads with no RPC to attach to. To make those visible to ASH:

- Create a `WaitStateInfo` for the task, give it a `FixedQueryId`, `Track()` it on the relevant
  `WaitStateTracker`, and `Untrack()` on teardown.
- Existing trackers (`src/yb/ash/wait_state.cc`): `FlushAndCompactionWaitStatesTracker`,
  `RaftLogWaitStatesTracker`, `SharedMemoryPgPerformTracker`, `XClusterPollerTracker`,
  `MinRunningHybridTimeTracker`, etc.
- The collector only samples trackers it knows about. If you add a **new** tracker, wire it into
  `ActiveSessionHistory()` in `src/yb/tserver/pg_client_service.cc` (next to the existing
  `AddWaitStatesToResponse(... Tracker() ...)` calls), otherwise the state will be entered but never
  sampled.

### Choosing the query id — ASK THE USER for background tasks

Every ASH sample carries a `query_id`. For a **user-driven (RPC) path** the query id flows in
automatically from the YSQL/YCQL layer — do nothing.

For a **background/non-RPC task there is no user query id**, so you must assign a `FixedQueryId`
(`src/yb/ash/wait_state.h`, e.g. `kQueryIdForFlush`, `kQueryIdForCompaction`). This is a deliberate
choice, not something to invent silently — **STOP and prompt the user**:

> "This path is a background task with no user query id. Should I (a) reuse an existing `FixedQueryId`
> (which one?), or (b) add a new `FixedQueryId` entry for it?"

- **Reuse** when the work belongs to an existing background category (e.g. another flush/compaction
  step → `kQueryIdForFlush`).
- **Add a new one** when it is a distinct category users should be able to filter on. Append it to
  the `FixedQueryId` enum with the next explicit number (do not reorder existing entries).

Set it on the wait state via `UpdateMetadata({.query_id = std::to_underlying(FixedQueryId::k...)})`
(see `flush_job.cc`, `compaction_job.cc`, `xcluster_poller.cc`).

## Plumbing ASH metadata through RPCs

`AshMetadata` (query_id, root_request_id, etc.) must follow any sub-RPCs launched. Use the proto
options defined in `src/yb/rpc/service.proto`:

- `option (yb.rpc.service_send_metadata) = true;` — send metadata with **all** RPCs of a service.
- `option (yb.rpc.send_metadata) = true;` — opt **in** for a single RPC (when the service flag is off).
- `option (yb.rpc.exclude_metadata) = true;` — opt **out** for one RPC (when the service flag is on).

The thread creating the OutboundCall must have the correct wait state adopted (see the async
pattern). On the receiver, the thread that created the InboundCall has the metadata.

## Threading ASH metadata through the master

The master is **not** sampled by the ASH collector (the sampler runs in `pg_client_service` on the
tserver). But many user operations are orchestrated by the master and do their real work on tservers
(e.g. index backfill, alter table). For ASH to attribute that tserver work to the originating query,
the wait-state metadata must be threaded **through** the master layer and onto the downstream RPCs —
the master is a pass-through, not an endpoint.

Master async tasks (`AsyncTabletLeaderTask` subclasses, `BackfillTable`/`BackfillTablet`) run later
on a callback pool, decoupled from the thread that received the originating RPC, so you cannot rely
on the current thread's wait state at execution time. Pattern (see `src/yb/master/backfill_index.{cc,h}`
and `src/yb/master/async_rpc_tasks.{cc,h}`):

1. **Capture** the wait state when the task object is constructed (still on the originating thread):
   store `ash::WaitStateInfo::CurrentWaitState()` in a member, falling back to a fresh
   `std::make_shared<ash::WaitStateInfo>()` (or `CreateIfAshIsEnabled<ash::WaitStateInfo>()` and copy
   metadata via `wait_state_->UpdateMetadata(current_state->metadata())`). Optionally tag the path
   with `UpdateAuxInfo({.method = "BackfillIndex"})`.
2. **Adopt** it in every entry point that runs on a pool thread — `SendRequest`, `HandleResponse`,
   `UnregisterAsyncTaskCallback`, launch/run methods — via `ADOPT_WAIT_STATE(wait_state_)`.
3. **Send it downstream**: set `option (yb.rpc.send_metadata) = true;` on each master→tserver RPC that
   must carry the metadata (e.g. `MasterDdl.BackfillIndex` in `master_ddl.proto`; `AlterSchema`,
   `GetSafeTime`, `BackfillIndex`, `BackfillDone` in `tserver_admin.proto`).

## Instrumenting a wait event in the pg_client service layer (`Perform` path)

The collector filters out the high-frequency control RPCs before sampling. `ShouldIgnoreCall(...)`
in `src/yb/tserver/pg_client_service.cc` (~2398) drops any call whose `aux_info().method()` is
`ActiveSessionHistory`, `AcquireAdvisoryLock`, or `Perform` when `req.ignore_ash_and_perform_calls()`
is set. The real ASH sampler **always** sets that flag (`pg_client.cc:1756`), so in production these
RPCs are filtered.

`Perform` is the main YSQL data path, so most pg_client service-layer work runs under a `Perform`
call. A wait state you add there is dropped by this filter unless its code is explicitly allowlisted.
There is one carve-out today: `kXCluster_WaitForSafeTime`.

So when instrumenting a wait event reachable from the pg_client service layer (inside a `Perform`
handler), **add your `WaitStateCode` to the `Perform` allowlist in `ShouldIgnoreCall`** alongside
`kXCluster_WaitForSafeTime`, or the sample is silently discarded in production:

```cpp
if (method == "Perform" &&
    wait_state.wait_state_code() != std::to_underlying(ash::WaitStateCode::kXCluster_WaitForSafeTime) &&
    wait_state.wait_state_code() != std::to_underlying(ash::WaitStateCode::kYourNewCode)) {
  return true;
}
```

Watch for the false pass: `wait_states-itest` may not set `ignore_ash_and_perform_calls`, so the
"entered" test can pass while the production sampler drops the sample. The `pg_ash-test.cc` query-id
test queries `yb_active_session_history` (the real flagged path) and will catch a missing allowlist
entry.

## Test it

### 1. Wait state is entered — `wait_states-itest`

Add the code to `INSTANTIATE_TEST_SUITE_P` in `src/yb/integration-tests/wait_states-itest.cc`, then:

```bash
# from repo root
./yb_build.sh release --cxx-test wait_states-itest --gtest_filter "*<YourCode>*"
```

### 2. Query id is correct — `pg_ash-test` (REQUIRED for every new path)

`wait_states-itest` only proves the state was *entered*; it does not check the `query_id`. You must
also add a test in `src/yb/yql/pgwrapper/pg_ash-test.cc` that confirms the ASH sample for the new
wait event carries the **expected query_id**. Pick the pattern that matches the path:

- **User-driven (RPC) path** — assert the sample carries the real query id from the user statement
  (pattern: `TestTServerMetadataSerializer`, ~line 990):

  ```cpp
  auto query_id = ASSERT_RESULT(conn_->FetchRow<int64_t>(
      "SELECT queryid FROM pg_stat_statements WHERE query LIKE '<your stmt>%'"));
  // ... drive the workload ...
  auto count = ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
      "SELECT COUNT(*) FROM yb_active_session_history "
      "WHERE query_id = $0 AND wait_event = '<YourCode>'", query_id)));
  ASSERT_GT(count, 0);
  ```

- **Background / non-RPC task** — assert the sample carries the chosen `FixedQueryId`
  (pattern: `TestMinRunningHybridTimeWaitEventHasTabletId`, ~line 1013):

  ```cpp
  const auto query_id = std::to_underlying(ash::FixedQueryId::kQueryIdFor<...>);
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(conn_->FetchRow<int64_t>(Format(
        "SELECT COUNT(*) FROM yb_active_session_history "
        "WHERE query_id = $0 AND wait_event = '<YourCode>'", query_id))) > 0;
  }, 30s * kTimeMultiplier, "wait for <YourCode> ASH sample"));
  ```

The `COUNT(*) FROM yb_active_session_history WHERE query_id = $0 ...` assert above is the same shape
in every test. Prefer a shared helper for it if one exists in the ASH test utilities (or factor one
out) rather than re-inlining the raw SQL in each new test.

Run it:

```bash
./yb_build.sh release --cxx-test pg_ash-test --gtest_filter "*<YourTestName>*"
```

For a background/non-RPC state, also confirm its tracker is wired into `ActiveSessionHistory()` so
the collector samples it (otherwise the query above will never return a row). Manual check:

```sql
SELECT DISTINCT wait_event, query_id FROM yb_active_session_history WHERE wait_event = '<YourCode>';
SELECT * FROM yb_wait_event_desc WHERE wait_event = '<YourCode>';
```

See `src/AGENTS.md` for general build/test guidance.

## Common pitfalls

- Async path without `ASH_ENABLE_CONCURRENT_UPDATES()` → DFATAL in `~ScopedWaitStatus`, test
  failures.
- Using `SCOPED_WAIT_STATUS` for async handoff → state cleared before the work runs; sample missed.
- New background tracker not wired into `ActiveSessionHistory()` → state entered but never collected.
- Master-orchestrated work (backfill, alter) where the metadata is not threaded through the master
  tasks (capture in ctor, `ADOPT_WAIT_STATE` in each pool-thread entry point, `send_metadata` on the
  downstream RPC) → tserver-side samples show `query_id = 0` instead of the originating query.
- Wait event on a `Perform` (pg_client service-layer) path not added to the `ShouldIgnoreCall`
  allowlist → `wait_states-itest` passes but the real sampler drops the sample in production.
- Forgetting the `GetWaitStateType`, `GetWaitStateDescription`, **or `GetWaitStateAuxDescription`**
  case for a new code. All three switches lack a `default`; the last is the one most often missed and
  it breaks the build.
- `SCOPED_WAIT_STATE` / `SET_WAIT_STATE` do not exist — the macros are `SCOPED_WAIT_STATUS` /
  `SET_WAIT_STATUS`.

## Self-check before finishing

Verify the change covers all that apply:

- [ ] Instruments TServer/DocDB code under `src/yb`, not PG backend code (`src/postgres`).
- [ ] `WaitStateCode` enum entry in the right `Class` group (`wait_state.h`).
- [ ] `GetWaitStateType` case returning the resource it waits on most (`wait_state.cc`).
- [ ] `GetWaitStateDescription` case (`wait_state.cc`).
- [ ] `GetWaitStateAuxDescription` case (`wait_state.cc`) — third no-`default` switch; missing = build break.
- [ ] Right macro at the wait site: `SCOPED_WAIT_STATUS` for sync; `SET_WAIT_STATUS` (caller) +
      `ADOPT_WAIT_STATE` (worker/callback) for async — never a bare scoped on the caller for async.
- [ ] Entered test: code added to `INSTANTIATE_TEST_SUITE_P` in `wait_states-itest.cc`.
- [ ] Query-id test in `pg_ash-test.cc` (user-driven → real `query_id`; background → `FixedQueryId`).
- [ ] Async path: `ASH_ENABLE_CONCURRENT_UPDATES()` at the handoff.
- [ ] Background/non-RPC path: `Track`/`Untrack` on a `WaitStateTracker`, and a new tracker wired into
      `ActiveSessionHistory()` in `pg_client_service.cc`.
- [ ] Background/non-RPC path: asked the user whether to reuse or add a `FixedQueryId` (not invented
      silently).
- [ ] pg_client service-layer (`Perform` path) wait event: code added to the `Perform` allowlist in
      `ShouldIgnoreCall` (`pg_client_service.cc`), else the production sampler drops it.
- [ ] Master-orchestrated path: wait state captured in the task ctor, `ADOPT_WAIT_STATE` in each
      pool-thread entry point, and `send_metadata` set on the downstream master/tserver RPCs.

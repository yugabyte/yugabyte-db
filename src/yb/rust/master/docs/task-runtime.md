# Task runtime (`master-task-runtime`)

How the master runs background work.

- **`TermScope`**: a cancellation token and task tracker for one leader term. Leadership cancels
  it on a term change or a reload before any token for the next phase exists; nothing starts in a
  cancelled scope.
- **`EntityScope`**: a child scope, so work for one entity can be cancelled without the term.
- **`Controller`**: `reconcile` runs once at start, on every change of its trigger, and at least
  every `interval`, until the scope is cancelled. Runs must be idempotent.
- **`RetryPolicy`**: exponential backoff with full jitter (uniform in [0, min(max, base * 2^n)]),
  bounded by attempts and a deadline. Defaults are the C++ master's (`retrying_ts_rpc_max_delay_ms`,
  `unresponsive_ts_rpc_retry_limit`, `unresponsive_ts_rpc_timeout_ms`); unlike C++, whose jitter
  comes from `rand_r` on a fixed seed, the jitter is random, so retries from many tasks spread out.
- **`BlockingPool`**: fixed threads with a bounded queue for one kind of blocking call. A full
  queue is a `Busy` error, never a wait: a task that blocks on work queued behind it deadlocks a
  bounded pool (the C++ `async_task_pool` had to be made unbounded for that reason).
- **`TsRpcExecutor`** (K2): RPCs to tservers in one FIFO per tserver and RPC kind, at most
  `limit(kind)` in flight, read on every send and completion so flag changes apply, a raised limit
  to queued calls too (`DeleteTablet`: `max_concurrent_delete_replica_rpcs_per_ts`). Waiting for a
  slot is not an attempt: backoff and the attempt budget apply only to failed sends, backoff
  happens outside the lane, and the overall deadline starts with the first send; no attempt
  starts after it. C++ counts a throttled send as a failed attempt and backs off, so drops of
  tables with many tablets ran out of time (#24013). A waiter that is cancelled passes its turn on.
- **`TaskRegistry`**: running tasks by name, for metrics and tests.

## Layer-2 work

| Where | Work |
|---|---|
| Every master | Catalog self-check (`BlockingPool` "catalog-check") |
| Leader term | `tserver-liveness` controller (1 s): marks tservers unresponsive and persists it |
| Leader term | `universe-uuid` controller (1 s): `SetUniverseUuidIfNeeded`, for the heartbeat-check AutoFlag promoted mid-term |
| Leader term | `replica-deletes`: sends the `DeleteTablet` calls report processing queued, through `TsRpcExecutor` |
| Leader term | `location-stream`: pushes locations to Rust followers |

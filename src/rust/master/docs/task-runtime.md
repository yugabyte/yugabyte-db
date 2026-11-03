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
- **`TaskRegistry`**: running tasks by name, for metrics and tests.

## Layer-2 work

| Where | Work |
|---|---|
| Every master | Catalog self-check (`BlockingPool` "catalog-check") |
| Leader term | `tserver-liveness` controller (1 s): marks tservers unresponsive and persists it |
| Leader term | `replica-deletes`: sends the `DeleteTablet` calls report processing queued, with `RetryPolicy` |
| Leader term | `location-stream`: pushes locations to Rust followers |

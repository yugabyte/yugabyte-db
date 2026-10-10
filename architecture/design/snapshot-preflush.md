# Snapshot preflush

`--snapshot_create_flush_before_submit=true` makes the tablet leader flush its captured replica
set before submitting `CREATE_ON_TABLET`. It is disabled by default because requiring every
replica changes snapshot availability: a healthy write quorum is insufficient if another replica
cannot finish its flush.

The leader pins snapshot history, resolves intents, captures the active Raft configuration, and
launches local flushing and follower `FlushTablets` RPCs concurrently. A bounded orchestration
pool waits for completion; flush I/O uses a separate bounded executor. Neither the preparer nor
`UpdateConsensus` waits for preflight. Writes and replication can continue, subject to normal
storage contention and write stalls.

Before submission, the leader rechecks its term, membership, and deadline. A failed attempt is
returned to the master for retry. Errors from a missing follower tablet are not reported as if
the leader's target tablet disappeared. There is no fallback that submits despite a failed flush.
The history guard stays alive through submission and is released when an attempt fails.

The captured set is the whole active configuration, including `PRE_VOTER`/`PRE_OBSERVER` peers
and read-replica observers, and membership comparison includes each peer's role. Consequences:
a peer still being remote bootstrapped fails its flush until it is running, so the snapshot stays
in `CREATING` and the master retries; its promotion during an attempt rejects that attempt once
more; and an unreachable read replica delays creation even though it never affects commit
latency. Restricting the set to voters would avoid these delays at the cost of observer
apply-time stalls, and is a policy change rather than a bug fix.

## Limits

| Flag | Default | Scope |
| --- | --- | --- |
| `snapshot_create_flush_before_submit` | `false` | Runtime; enables preflight on the leader |
| `snapshot_preflush_timeout_ms` | `10000` | Runtime; capped by the snapshot RPC deadline |
| `snapshot_preflush_concurrency` | `4` | Startup; admitted preflights per server |
| `tablet_flush_concurrency` | `4` | Startup; workers running admin flush jobs |
| `tablet_flush_max_outstanding` | `16` | Runtime; admitted flush jobs: reserved, queued, running, retiring; must exceed `snapshot_preflush_concurrency` |

Admission is non-waiting and excludes overlapping work for the same tablet. Overload is a
retryable error. The leader reserves its own flush slot together with the preflight slot, before
any follower is contacted, so a server cannot reject an attempt it has already fanned out; the
reservation is consumed by the local flush job or released when the attempt fails earlier.
Reserved jobs run on a lane of `snapshot_preflush_concurrency` workers: at most that many
reservations exist, so admin bulk flushes (for example a whole-table `FlushTables`, one batch per
server) occupying the shared workers cannot make an admitted preflight expire in the queue after
its followers already flushed. Other admitted jobs beyond the worker count wait in the shared pool;
a job whose deadline passed while queued fails without launching I/O and is counted in
`tablet_flush_expired_queued`. `tablet_flush_max_outstanding` must exceed
`snapshot_preflush_concurrency` (validated) so local reservations leave room for other servers'
requests. Sizing bound: a server can hold `snapshot_preflush_concurrency` reservations plus up to
`(N - 1) * max_concurrent_snapshot_rpcs_per_tserver` incoming follower flushes when every other
server's in-flight snapshot RPC targets a tablet with a replica here; that is a worst case under
full placement skew, not expected load, but it is where the default of 16 stops covering clusters
of roughly a dozen nodes. An RPC timeout does not stop a physical flush: receiver admission
remains held until the job finishes, and late completions cannot submit an expired snapshot
attempt. Partial failures stop further launches but retain the batch's reservations until
already-started work and RocksDB flush-job cleanup retire. The first error and its tablet ID
survive this draining phase.

A terminal vector failure fails dependent intents flushes without bypassing durability ordering.
Storage filter errors make the affected RocksDB read-only, even with paranoid checks disabled.
These error paths also apply to ordinary admin flushing when snapshot preflight is disabled.

Preflight completion does not establish a common flushed OpId or exclude later writes, replica
restarts, or membership changes. `TabletSnapshots::Create()` retains its synchronous all-DB flush
for correctness. A lagging follower or a busy tablet can still have significant apply-time work.

## Observation and rollback

`snapshot_preflush_duration_us` measures the preflight separately from apply-time
`Snapshot_WaitingForFlush`. The `snapshot_preflush_active`, `snapshot_preflush_failures`,
`snapshot_preflush_timeouts`, and `snapshot_preflush_rejections` metrics expose admission and
failure behavior. `tablet_flush_active` counts admitted receiver jobs, including failed jobs still
draining; `tablet_flush_pool` metrics expose worker queue and run time. A job can contain multiple
tablets and storage flushes, so occupancy is not a count of all physical RocksDB flushes.

Disable the feature flag to return to direct snapshot submission and the final synchronous flush.
There are no new wire fields, Raft entry types, or on-disk formats. In mixed-version deployments,
only leaders supporting and enabling the flag orchestrate preflight. Supported older followers
use the existing admin flush RPC; mixed versions do not provide a cluster-wide latency guarantee.

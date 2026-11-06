# Sys-catalog store (`master-sys-catalog`)

The master's view of the sys-catalog tablet: scans at a read time, Raft writes in a term, and the
apply stream. The C++ host implements `SysCatalogStore` (`master-server/src/store.rs`);
`FakeSysCatalog` is an in-memory log with MVCC history for tests.

## The apply stream

Every master, leader or follower, learns about catalog rows only from what its tablet applies
(L1). The host wraps `MasterSnapshotCoordinator` in `SysCatalogTap`
(`src/yb/master_host/sys_catalog_tap.{h,cc}`):

- `Tablet::ApplyKeyValueRowOperations` already calls the coordinator's `ApplyWritePair` for every
  applied write pair. The tap decodes the key (`(entry_type, entry_id)`, co-table keys skipped)
  and the value (a string, or a tombstone for a delete), and forwards the call unchanged.
- After the pairs of one op, the tablet calls `AppliedWriteBatch(op_id, write_time)`, the only
  change to tablet code. The tap sends the batch to Rust as `ApplyEvent::Rows`.
- Changes that do not pass through `ApplyKeyValueRowOperations` post `ApplyEvent::Reload`
  instead: the tablet opening, a PITR restore (`RestoreSysCatalogOnApply`), a remote bootstrap,
  and shell mode. A write pair the tap cannot decode also posts a Reload (and is `DFATAL`), and so
  does a packed row (an `INSERT` through `WriteSysCatalogEntry` in emergency repair mode, where
  packed rows are on).

Apply must never block, so the stream is an unbounded queue; the sender logs a warning every
10000 queued events. The consumer does no I/O per event.

`ApplySender::barrier()` resolves once every event queued before it is processed. Because the
tap sends a batch before the write that produced it completes, a writer that awaits its write and
then a barrier knows the materializer has applied it. The sender also records the op index of the
last batch sent and counts Reloads as they are sent, before they are received.

## Writes

`write_rows(term, rows)` is one Raft write built like `SysCatalogTable::SyncWrite`'s, through
`FillSysCatalogWriteRequest`, but asynchronous. The `WriteQuery` carries the term, so Raft
refuses it once this peer is no longer leader in that term. It resolves once the write is
applied locally.

## Scans

`scan(entry_type, read_time)` reads one entry type at a hybrid time, and `safe_time(min, timeout)`
waits for the tablet's safe time to reach `min`. A load scans every type at the same safe time.

## Invariants

- Sys-catalog rows are `(entry_type, entry_id) -> metadata`, written without transactions.
- The tap sends a batch after writing it, so a batch at or below an op index read before a scan
  is in a scan at or above its hybrid time. A batch sent later may carry a hybrid time at or below
  the scan's: snapshot ops bypass MVCC. The materializer relies on both to splice a load into the
  stream.

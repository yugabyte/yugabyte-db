# YSQL catalog follower reads: initial scope

The initial rollout targets authentication catalog reads in universes that do not
use PITR. This layer implements admission policy only; master `Read` RPCs remain
leader-only. Snapshot selection and follower routing are separate changes.

## Permanent operational restriction

**Reserving a universe for catalog follower reads permanently excludes PITR in
this release. Disabling follower-read routing does not undo the reservation.**

- Reservation requires no snapshot schedules, retained schedule snapshots, or
  schedule restorations. Deleted but not yet cleaned-up PITR state still blocks it.
- After reservation, creating PITR schedules and restoring the system catalog are
  prohibited across the universe, including YCQL schedules: the masters share one
  physical system-catalog tablet.
- Restart, leader change, and capability-AutoFlag demotion do not unlock PITR.
- There is no release/unreserve API or supported in-place conversion, with or
  without downtime. Do not opt in if this universe will need PITR later.
- Ordinary backup snapshots and restores that do not restore the system catalog
  remain available. Workflows that require PITR schedules are excluded.

The `ReserveYsqlCatalogFollowerReads` master RPC requires
`acknowledge_permanent_pitr_exclusion=true`. Its name and acknowledgement describe
a durable reservation, not a routing flag. Repeating an acknowledged reservation
is idempotent. A new universe is not reserved by default.

## Why exclude restore

Normal MVCC writes preserve historical snapshots. System-catalog restore can
change the historical data visible to a read. Supporting both features requires
restore-overlap checks, durable read boundaries, recovery rules, and cross-replica
restore validation. The first rollout avoids that machinery by prohibiting the
conflicting operation before any follower reads are enabled.

## Admission and persistence

The reservation is a dedicated `SysConfigEntryPB` row in the replicated system
catalog. The snapshot coordinator loads and applies it on every master replica.
It is not part of the mutable cluster-configuration document, so unrelated admin
updates cannot clear it by omitting an unfamiliar field.

Immutability is enforced by the supported administrative APIs: the reservation
producer only writes true and there is no removal operation. Post-write callbacks
validate the stored form for diagnostics; they do not veto storage writes.
Direct system-catalog edits in emergency repair mode are outside this contract
and are not a supported way to release the reservation.

PITR admission and reservation use the snapshot coordinator's mutex. The first
admitted side blocks the conflicting side for that leader term. The durable
reservation continues to block PITR in later terms.

A client timeout does not cancel a Raft write. A timed-out or failed admission
therefore remains conservative: the conflicting mode may remain unavailable until
a new leader catches up and resolves the durable state. The new leader must be
ready before handling requests; stale leader terms cannot reset admission state.
This covers requests paused before submission as well as writes awaiting apply.

`GetYsqlCatalogFollowerReadReservation` reports the committed reservation, pending
reservation admission, PITR admission in the current term, and that leader term
without changing state. Pending and permanent PITR rejection messages differ.
An acknowledged retry can finish a reservation after a timeout. A leader change
can resolve transient uncertainty, but cannot release a committed reservation.

The read path must not treat the reservation alone as a fresh snapshot. Future
routing must require a leader-established authentication snapshot and proof that
the selected replica has applied the reservation. All reads in an attempt, including
leader fallback, must retain the selected snapshot. Shared response-cache semantics
remain separate work.

## Upgrade/Rollback safety

`ysql_enable_catalog_follower_read_reservation` is a `kLocalPersisted` AutoFlag,
initially false and targeting true. Promotion permits reservations; it does not
reserve a universe or enable routing. Install compatible binaries on every master
peer before promotion or reservation. Promotion alone is not proof that every
master is compatible. Do not use local flag overrides during partial upgrades.

The operational states differ:
- Unpromoted and unreserved: the capability is unavailable; PITR is unchanged.
- Promoted but unreserved: PITR is unchanged, but old binaries can reject the
  unknown promoted flag at startup. Explicitly demote it before older-binary
  rollback. Generic AutoFlag rollback does not roll back persisted-class flags.
- Reserved: demotion stops new reservations but does not release this universe.
  **Downgrade to binaries that do not enforce the reservation is not supported.**
  Those binaries could admit PITR. A routing flag or capability demotion is not
  a conversion or rollback procedure.

## Review focus

Verify both admission orderings, including timeouts before a write is submitted.
Check reservation persistence through replay, restart, and leader change; retained
PITR state must prevent activation. Ordinary backup operations must stay available.
No test-only follower-read bypass is part of this layer.

# YSQL catalog follower reads: PITR exclusion

Authentication follower reads need a system catalog whose historical snapshots
cannot be replaced by restore. This layer supplies that prerequisite only;
master reads remain leader-only. Snapshot acquisition and routing follow separately.

## Creation-only mode

Create a new universe with the experimental, non-runtime master flag
`--disable_pitr=true` to permanently disable PITR. It defaults to false.

- Existing default-mode universes cannot opt in, even without PITR history.
- Servers reject PITR schedules and system-catalog restore across the universe,
  including YCQL. The masters share one physical system-catalog tablet.
- Omitting the flag, restarting, or changing leaders does not undo the mode.
- No in-place conversion is supported in either direction.
- Ordinary backup snapshots and data-only restores remain available.

Inspect `pitrDisabled` through `yb-admin get_universe_config`. There is no
reservation RPC, acknowledgement, capability promotion, or pending admission state.

## Persistence and recovery

Bootstrap stores `SysClusterConfigEntryPB.pitr_disabled` in the replicated system
catalog. Every replica loads and applies it. Administrative config replacement
rejects changes and preserves creation fields omitted by clients.

An internal template marker distinguishes interrupted creation from an existing
universe. Recovery preserves an already committed mode rather than restoring the
template over it. Use a matching initial snapshot; regenerate it with
`yb_build.sh ... reinitdb` when reusing a build directory.

The mode does not prove read freshness. Later follower routing must use a
leader-established timestamp and retain it through paging and leader fallback.

## Upgrade/Rollback safety

All masters must support this mode before universe creation. Do not downgrade
PITR-disabled data to mode-unaware binaries: those binaries could admit PITR.
A startup-flag change is not a rollback or conversion procedure.

Old experimental reservation data is unsupported and rejected. Create new data
directories rather than convert it. Direct system-catalog edits in emergency
repair mode are outside this contract.

## Validation

Use `pitr_disabled-test`, one case per invocation, to check default-mode PITR,
creation-only selection, immutable config replacement, replica/restart/failover
persistence, interrupted initdb, legacy-metadata rejection, and ordinary backups.

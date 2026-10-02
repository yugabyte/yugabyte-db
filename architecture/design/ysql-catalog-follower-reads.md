# YSQL catalog follower reads: PITR exclusion

Authentication follower reads need a system catalog whose historical snapshots
cannot be replaced by restore. This layer supplies that prerequisite only;
master reads remain leader-only. Snapshot acquisition and routing follow separately.

## Permanent startup opt-in

`--disable_pitr=true` is a new experimental, non-runtime master flag in this stack,
not an existing released setting. It defaults to false. It can select permanent
PITR exclusion when creating a universe or during a coordinated master restart
of an eligible existing universe. It does not enable follower routing.

- Servers reject PITR schedules and system-catalog restore across the universe,
  including YCQL. The masters share one physical system-catalog tablet.
- Omitting the flag, restarting, or changing leaders does not undo the mode.
- Re-enabling PITR in place is unsupported.
- Ordinary backup snapshots and data-only restores remain available.

Inspect `pitrDisabled` through `yb-admin get_universe_config`. There is no
reservation RPC, acknowledgement, capability promotion, or pending admission state.

## Existing-universe activation

Install compatible binaries on every master before requesting activation. Keep
follower routing off. Inspect schedules, snapshots, and restorations using the
backup APIs before scheduling the control-plane outage.

Activation requires no schedule metadata (including deleted schedules awaiting
cleanup), no retained schedule snapshots, and no unfinished or incompletely
recorded restoration. Ordinary snapshots and fully finalized restoration history
do not disqualify a universe. Past PITR use alone is not a permanent restriction.
No state is deleted or aborted automatically to make the universe eligible.

Stop all masters before restarting any of them with `--disable_pitr=true`. This
initial rollout does not support rolling or live activation. The elected master
finishes recovery, checks eligibility, and commits the mode before becoming ready.
The existing configuration and universe identifiers are preserved.

A blocker refuses startup and reports the offending object. Restart without the
request to recover normal service and explicitly finish or clean up the blocking
work. A crash or write timeout may occur after commitment: read the persisted mode
after recovery instead of inferring the outcome from startup success or failure.
Only enable follower routing after confirming the durable mode.

## Persistence and recovery

Each process captures startup intent before RPC services start; forcing a flag
change on a running process cannot request activation. The flag is not the
effective mode: `SysClusterConfigEntryPB.pitr_disabled` is replicated and applied
on every master. Administrative config replacement rejects changes and preserves
protected fields omitted by clients.

The check/write sequence runs under the leader initialization barrier, not on a
serving leader. A process requesting exclusion cannot become ready without the
durable mode, so it cannot first admit PITR and then switch in the same process.
Already-disabled universes do not recheck eligibility on restart: an ordinary
data restore may legitimately be in progress.

An internal template marker identifies prebuilt initdb metadata. Recovery keeps
an already committed mode rather than restoring a template over it. Use a matching
initial snapshot; regenerate it with `yb_build.sh ... reinitdb` in reused builds.

The mode does not prove read freshness. Later follower routing must use a
leader-established timestamp and retain it through paging and leader fallback.

## Upgrade/Rollback safety

All masters must support this mode before opt-in. Do not downgrade PITR-disabled
data to mode-unaware binaries: those binaries could admit PITR. A routing or
startup-flag change does not undo the persisted policy.

Old experimental reservation data remains unsupported and rejected. Direct
system-catalog edits in emergency repair mode are not a supported conversion.

## Validation

Use `pitr_disabled-test`, one case per invocation, for startup eligibility,
runtime-mutation rejection, preserved configuration, old PITR history, interrupted
activation, flagless recovery, immutable updates, and ordinary restore recovery.
New-universe bootstrap and default-mode PITR must continue to work.

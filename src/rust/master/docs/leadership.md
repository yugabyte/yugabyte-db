# Leadership (`master-leadership`)

Turns the sys-catalog Raft state into the phases the core acts on, and issues the token every
catalog write carries.

```text
Follower -> CatchingUp(t) -> Ready(t, p) <-> LeaseLapsed(t, p)
                               Ready(t, p) -> Reloading(t) -> Ready(t, p + 1)
any phase -> Follower when the term changes or the peer is no longer leader
```

- **CatchingUp.** Elected in term t. Waits for `LEADER_AND_READY` (the term's NoOp committed and
  the lease held), then runs the ready hooks in order, retrying a failed hook. `master-server`
  registers one: a catalog barrier, so the catalog reflects everything committed before the
  election, then `TsManager::on_leader_ready` (tservers must send full reports, registrations get
  a heartbeat grace period, warm locations become unconfirmed).
- **Ready.** A new `TermScope` (`master-task-runtime`) starts the term's controllers.
- **LeaseLapsed.** Same term, lease lost: no tokens, but the scope keeps running.
- **Reloading.** A catalog reload was requested while leading (a restore, a self-check mismatch,
  an undecodable write). `master-server` enters it as soon as the reload is requested, so no token
  is issued while the snapshot is known to be stale, and leaves it once the reloaded catalog is
  published: Ready resumes with a new scope and `pitr_count + 1`, so nothing decided before the
  reload is written after it.

The Raft state is polled every 100 ms; an election also nudges it through the apply sink. The
host reads the term from the consensus state: `GetLeaderState()` carries it only when
`LEADER_AND_READY`, and -1 otherwise.

## LeaderToken

`LeaderToken {term, pitr_count}` is created only by `Leadership::token()`, which also checks the
live Raft state, so a lapsed lease is seen on the next call rather than the next poll. The writer
refuses a mutation whose token is not current; the Raft write also carries the term, so a write
from a deposed leader fails even if it passed the token check.

## Readiness errors

`token()` fails with what `ScopedLeaderSharedLock` reports, which handlers put in
`NOT_THE_LEADER`:

| Phase | Status |
|---|---|
| Follower | IllegalState "Not the leader" |
| CatchingUp | LeaderNotReadyToServe "yb-master leader is initializing" |
| LeaseLapsed | the `LeaderState::CreateStatus` status (LeaderHasNoLease, LeaderNotReadyToServe) |
| Reloading | ServiceUnavailable "Catalog manager is restoring" |

## Leading is a test-only feature in layer 2

A layer-2 Rust leader serves only the core RPCs; in a quorum with C++ masters it would leave DDL,
snapshots, the load balancer and YSQL leases unserved. So `--TEST_rust_master_allow_leadership`
(default false, defined by the C++ host because it decides failure detection at startup) gates
it. Without the flag the host keeps layer 1's behavior: no failure detection, and an immediate
step-down if a leader transfer elects it.

PITR restores cannot start while a Rust master leads in layer 2: the restore RPCs are not served.
Restores applied while following are covered by the catch-up barrier.

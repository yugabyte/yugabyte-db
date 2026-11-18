# Leadership (`master-leadership`)

Turns the sys-catalog Raft state into the phases the core acts on, and issues the token every
catalog write carries.

```text
Follower -> CatchingUp(t) -> Ready(t, p) <-> LeaseLapsed(t, p)
                               Ready(t, p) -> Reloading(t) -> Ready(t, p + 1)
any phase -> Follower when the term changes or the peer is no longer leader
```

- **CatchingUp.** Elected in term t. Waits for `LEADER_AND_READY` (the term's NoOp committed and
  the lease held), then runs the ready hooks in order, retrying a failed hook. A hook that keeps
  failing for 60 s ends the process, as C++ does with `LOG(FATAL)` when `VisitSysCatalog` fails, so
  another master can lead. `master-server` registers one: a catalog barrier, so the catalog
  reflects everything committed before the election; the missing default rows
  ([catalog-state.md](catalog-state.md)); then `TsManager::on_leader_ready` (tservers must send
  full reports, registrations get a heartbeat grace period, warm locations become unconfirmed).
- **Ready.** A new `TermScope` (`master-task-runtime`) starts the term's controllers.
- **LeaseLapsed.** Same term, lease lost: no tokens, but the scope keeps running.
- **Reloading.** A catalog reload was requested while leading (a restore, a self-check mismatch,
  an undecodable write). `master-server` enters it as soon as the reload is requested, so no token
  is issued while the snapshot is known to be stale, and leaves it once the reloaded catalog is
  published: Ready resumes with a new scope and `pitr_count + 1`, so nothing decided before the
  reload is written after it.

Ready hooks capture leader components that hold `Leadership`, so `master-server` calls
`Leadership::shutdown()` (drops the hooks, cancels the scope) once its runtimes have stopped;
without it the whole leader stays reachable through the cycle and LeakSanitizer reports it.
Components that call back into one another (snapshot, xCluster and failover hooks, the xrepl
extension) hold the back-references as `Weak` for the same reason.

The Raft state is polled every 100 ms; an election also nudges it through the apply sink. The
host reads the term from the consensus state: `GetLeaderState()` carries it only when
`LEADER_AND_READY`, and -1 otherwise.

## LeaderToken

`LeaderToken {term, pitr_count}` is created only by `Leadership::token()`, which also checks the
live Raft state, so a lapsed lease is seen on the next call rather than the next poll. The writer
refuses a mutation whose token is not current; the Raft write also carries the term, so a write
from a deposed leader fails even if it passed the token check.

`catch_up_token(term)` gives the ready hooks a token while the term is CatchingUp, once its NoOp
is committed and the lease is held, so a hook can write before the term is Ready (a new
universe's default rows, as C++ writes them before setting `leader_ready_term_`). `check` accepts
it while the term catches up, and it equals the Ready token afterwards.

## Readiness errors

`token()` fails with what `ScopedLeaderSharedLock` reports, which handlers put in
`NOT_THE_LEADER`:

| Phase | Status |
|---|---|
| Follower | IllegalState "Not the leader" |
| CatchingUp | LeaderNotReadyToServe "yb-master leader is initializing" |
| LeaseLapsed | the `LeaderState::CreateStatus` status (LeaderHasNoLease, LeaderNotReadyToServe) |
| Reloading | ServiceUnavailable "Catalog manager is restoring" |

A Rust master takes part in elections as a C++ master does, and leads when it wins one.

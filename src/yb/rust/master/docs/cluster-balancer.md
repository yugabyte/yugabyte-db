# The cluster balancer (`master-cluster-balancer`)

The balancer's policies are today's C++ ones (K8): `ClusterLoadBalancer` and `PerTableLoadState`
(`src/yb/master/cluster_balance.cc`, `cluster_balance_util.cc`), ported as a pure planner. A run
takes a `ClusterView` and returns the actions to send, the warnings that kept it from progressing,
and its metrics. No new balancing algorithm: every balancer fix in C++ is a behavioral rule, and
`load_balancer_mocked-test.cc` is ported scenario by scenario (`src/tests.rs`) to keep them fixed.

## Inputs and outputs

- `ClusterView`: every registered tserver (`TsInfo`: cloud info, placement uuid, liveness, time
  since heartbeat, blacklisted or leader-blacklisted, disk use, pending deletes), the cluster
  config's live and read replica placement uuids, and the tables. A table carries its effective
  replication info, its tablets with their observed replicas (role, Raft state, member type, data
  directory, size, `should_disable_lb_move`), and the balancer's tasks in flight for it.
- `Action`: `AddReplica` (ADD_SERVER as PRE_VOTER, or PRE_OBSERVER for a read replica placement,
  with the source of a move), `RemoveReplica` (REMOVE_SERVER) and `MoveLeader` (a stepdown to a
  peer, or to any peer, then the remove if `also_remove_replica`).
- `Activity` per placement (live first, then each read replica cluster), as C++ records them in
  its activity buffer, and `RunMetrics` summed over them (`tablets_in_wrong_placement`,
  `blacklisted_leaders`, `total_table_load_difference`, `estimated_data_to_balance_bytes`).
  `ActivityBuffer` keeps the last `load_balancer_num_idle_runs`; the balancer is idle when all
  were: no warnings, no tasks in flight, no actions and no replica still starting.

The caller (the balancer controller) builds the view, sends the actions, tracks them as tasks
and passes them back in the next view. The tasks in flight are applied to the state before
deciding, consume the global add, remove and leader-move budgets, and keep a tablet from getting a
second task of the same kind. Like C++, the planner keeps nothing else between runs except whether
global balancing is allowed (from the first idle run until a move that is not global).

## The controller

`controller::BalancerController` runs each leader term, every `catalog_manager_bg_task_wait_ms`,
once `load_balancer_initial_delay_secs` have passed since the term began and while
`enable_load_balancing` and `ChangeLoadBalancerState` allow it. Each pass builds the view:

- tservers from the registry: live and reported (`IsLiveAndHasReported`), blacklisted or
  leader-blacklisted by address, disk use summed over the last metrics heartbeat's paths, and the
  replica deletes queued or in flight to them;
- tables from the catalog snapshot, skipping system tables, colocated tables other than the parent
  and tables being dropped, with `table_replication_info` (the table's, else the cluster's);
- replicas from the observed locations (role, state, member type, data directory, size). Whether
  a config is in transition (a PRE_VOTER) is read from the observed replicas, which hold the
  leader-reported config, rather than by parsing every tablet row each pass.

Actions become tasks of their table for the term, in the view's `pending` until they finish:

- `AddReplica` and `RemoveReplica`: `ChangeConfig` (ADD_SERVER with the tserver's addresses and
  cloud info, or REMOVE_SERVER) on the tablet's leader, with the committed config index as the
  compare-and-set index. An attempt is dropped once the tablet is no longer RUNNING or a newer
  config is committed; CAS_FAILED, ADD_CHANGE_CONFIG_ALREADY_PRESENT,
  REMOVE_CHANGE_CONFIG_NOT_PRESENT, LEADER_NEEDS_STEP_DOWN and NOT_THE_LEADER end it, as in
  `AsyncChangeConfigTask`; other errors are retried.
- `MoveLeader`: one `LeaderStepDown` to the leader with the new leader, then the REMOVE_SERVER if
  asked. A failed stepdown keeps the planner from nominating the same new leader for
  `min_leader_stepdown_retry_interval_ms`. C++ registers the failure under the old leader's uuid
  but looks it up by the new leader's; the controller records it by the new leader.

`BalancerStatus` keeps the last `load_balancer_num_idle_runs` placement runs and the last run's
metrics for the RPCs: `IsLoadBalancerIdle` and `IsLoadBalanced` (idle, or disabled; enough live
tservers), and `GetLoadMoveCompletion` / `GetLeaderBlacklistCompletion` (replicas or leaders still
on blacklisted tservers against the blacklist's initial load, 0% within
`blacklist_progress_initial_delay_secs` of the election). `AreLeadersOnPreferredOnly` checks that
every leader of a running table is on a live, non-blacklisted tserver of the live placement that
the table's leader affinity accepts (`IsAcceptingLeaderLoad`).

When `ChangeMasterClusterConfig` grows the leader blacklist, every live tserver is marked
(`MarkTServersForLeaderBlacklistNotification`). Once no leader-blacklisted live tserver reports
leaders, each marked tserver's next heartbeat response lists the leader-blacklisted tservers in
`leader_blacklisted_tservers_with_no_leaders` (with `send_leader_blacklisted_tservers_on_heartbeat`),
once, so it can stop sending them leader traffic.

## Order of a run

As `RunClusterBalancerWithOptions`: every table is analyzed first, since one table's adds change
the global loads the others sort by. Then under-replicated tablets get replicas, fewest replicas
first across tables. Then, per table: removals (wrongly placed or blacklisted replicas of
over-replicated tablets first), adds (off blacklisted or wrongly placed tservers, then out of
blocks above their maximum, then by load), and leader moves (across leader affinity priorities,
then within them).

## Where the port differs

- Where C++ iterates a hash map, the port iterates in key order, so ties C++ leaves to hash order
  are broken by uuid, path or table order. The ported scenarios that assert a choice are the ones
  C++ makes deterministic.
- `PerTableLoadState::RemoveRunningTablet` stops at the first data path that does not hold the
  tablet (an inverted check), so a removed replica can stay listed on its path; the port removes
  it from every path.

## Scale

`tests::scale` (ignored; `cargo test --release -p master-cluster-balancer scale -- --ignored
--nocapture`, sized by `SCALE_TSERVERS` and `SCALE_TABLETS`) times runs over one RF3 cluster with
an empty tserver. Measured on an 8-core dev server: 500 tservers and 500,000 tablets take about
4.5 s per run; 5,000 tservers and 5 million tablets take 94 to 135 s per run with a 26 GB peak
(the view included). The cost is the string-keyed maps and sets of the state, which C++ also
uses; interning tserver and tablet ids as indices is what the W8 target needs. The controller
runs the planner on a blocking thread so a long run does not hold an async worker.

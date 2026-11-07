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

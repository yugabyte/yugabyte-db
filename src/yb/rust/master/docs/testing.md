# Testing the Rust master

## Rust unit tests

`./yb_build.sh <type> --target rust_checks` runs `cargo fmt --check`, `cargo clippy -D warnings` and
`cargo test` for the workspace. `master-server`, `master-tserver-sim-main` and `yb-cpp-host` are excluded
from `cargo test`: their code calls into the C++ host, so it only links inside a CMake-built
executable and is covered by C++ tests.

`master-rpc-handlers`' `every_master_method_is_served` is the RPC coverage gate: it fails when a
method of a master service has no handler arm, unless `leader::NEVER_SERVED` lists it with the
reason.

## Sys-catalog row conformance

The Rust master must serialize every sys-catalog row exactly as the C++ master does (see
[src/yb/rust/docs/proto.md](../../docs/proto.md)). To check a real catalog:

```bash
# With the masters stopped:
build/latest/bin/sys-catalog-tool --fs_data_dirs <master data dir> dump-rows --file rows.tsv
build/latest/bin/master-conformance rows.tsv
```

`dump-rows` writes each row with the C++ runtime's re-serialization. `master-conformance` checks
that Rust produces the same bytes, and that unknown fields of every wire type appended to the row
survive. It exits non-zero on any mismatch.

Rows written by an older release are checked the same way. With the 2.25.0.0 release master
(catalog created by that master, tables created through it by `master-tserver-sim`), all 69 rows
re-serialize identically. That check is manual: the release binary is not part of the build.

## Integration tests

`src/yb/integration-tests/rust_master-itest.cc` runs quorums of Rust masters in external processes
(ExternalMiniCluster, YSQL off):

```bash
./yb_build.sh release --cxx-test rust_master-itest --gtest_filter RustMasterTest.FollowerServesRpcs
```

| Test | Checks |
|---|---|
| `FollowerServesRpcs` | A restarted follower stays a follower through YCQL DDL, an index, a snapshot schedule and a PITR restore; serves `GetMasterRegistration` and `ListMasters`; answers `NOT_THE_LEADER` to leader-only methods; the leader lists it as a healthy follower. |
| `AddedThroughRemoteBootstrap` | A shell-mode master is added with `ChangeConfig`, remote-bootstraps the sys catalog, follows a workload, serves `CheckMasterTabletHealth`, and is removed. |
| `LeaderServesWebJson` | The leader's JSON pages parse, `/api/v1/overview` counts the live tservers, the UI (or a page that says it was not built) is served at the old page URLs, and the host's `/ObjectLockManager` page answers. |
| `WebPages` | A follower serves the master list, version, leader-fetched JSON and HTML pages; `/masters` bolds the follower; `is-leader` is 503; `/varz` and `/prometheus-metrics` come through the proxy; the registration advertises the Rust port. |
| `WebServerAuthAndRedirect` | HTTP Digest against an htdigest file on Rust routes and proxied host pages, and a 302 to https for plain HTTP on the TLS port. |
| `WebServerHttps` | With the webserver TLS flags the Rust master serves HTTPS with HSTS, refuses plain HTTP, fetches leader pages from an HTTP leader, and advertises the configured interface. |
| `TserverSimulator` | `master-tserver-sim` registers ten fake tservers with the leader, and the leader lists them. |
| `FollowerCatalogMatchesRandomWorkload` | A follower's in-memory catalog (`MasterRustPeer.DumpCatalog`) equals the leader's sys catalog row for row after seeded random YCQL DDL, a snapshot schedule and a PITR restore; the restore reloaded the catalog, the self-check never disagreed, and all rows pass the conformance check. The seed is logged. |
| `RustLeaderServesCoreRpcs` | A follower takes leadership; real tservers register and report to it; it serves table locations with leaders, `IsMasterLeaderServiceReady`, a cluster config change, the catalog writer's metrics, and `NotSupported` for a never-served method. |
| `WarmFailover` | The leader's location stream reaches a follower, which, after a failover with every tserver stopped, serves non-stale locations and the tserver list. |
| `RustLeaderInformsRemovedMaster` | A master removed by the leader goes back to shell mode. |
| `LiveTserverListVersion` | Tservers echo the live-list version and the Rust leader answers "unchanged"; after a tserver stops, the others get the new list and keep heartbeating. |
| `CreatesUniverse` | A new universe gets the default rows (system namespaces and tables with their tablets, the default role, the configs), and every master holds the same rows. |

Quorums that mix C++ and Rust masters exist only during an upgrade from the last C++ release and a
rollback to it. `src/yb/integration-tests/upgrade-tests/rust_master_upgrade_rollback-itest.cc`
covers them against the R-1 release's C++ masters (downloaded by `UpgradeTestBase`): it upgrades
one, two or all masters to the Rust master and rolls back from each step.

### MiniCluster tests

MiniMaster runs the Rust master in the test process (see
[cpp-host.md](cpp-host.md#embedded-in-a-test-process)); MiniCluster's helpers (leader lookup,
`WaitForTabletServerCount`, `WaitForReplicaCount`) go through RPCs. `rust_master_mini_cluster-test`
checks leadership, a step-down and a follower restart with three masters, and YCQL keyspace and
table DDL served by them: a table's tablets are created on every replica and serve reads and writes
before and after a leader change, the new leader alters and drops it, and a transactional table
gets `system.transactions` created first.

### Reading master state in tests

Tests read the master only through RPCs.
`MasterTestApi` (`src/yb/integration-tests/master_test_api.h`, for a MiniCluster or an
ExternalMiniCluster) wraps the leader's RPCs that tests need: table and tablet locations, tserver
lists, the cluster config, balancer idleness, splits, sys-catalog flush and compaction, and
`DumpSysCatalogEntries`. `Catalog()` builds a read-only `CatalogSnapshot` of `TableInfo`,
`TabletInfo` and `NamespaceInfo` objects from the dumped rows, for tests written against the C++
objects; it holds the rows at the time of the call and never changes. `FindTable` skips DELETED
tables, so a test that looks for a table a DDL rollback may already have dropped must scan
`GetTables()` instead.

`MasterAdmin.SplitTablet` takes no split key, so the split helpers in `tablet-split-itest-base`
split where the tablet leader chooses; a test cannot split at a chosen hash code.

`WaitForInitDb` asks `IsInitDbDone` over RPC, and `MiniCluster::WaitForTabletServerCount` returns the
`ListTabletServers` entries.

## Tserver simulator and the C++ baseline

`master-tserver-sim` runs many fake tservers in one process, each on its own loopback address
(`tserver_sim_first_ip` plus i, port `tserver_sim_rpc_port`). Each registers, heartbeats every
`tserver_sim_heartbeat_interval_ms` with full and incremental tablet reports, and answers
`CreateTablet`, `DeleteTablet` and `AlterSchema` from memory. A new replica is RUNNING at once, and
the peer with the lowest UUID leads every Raft group without an election. Other tablet and
consensus calls are answered with an empty response, or an error when the response has required
fields, and counted under `unmodeled_calls`. After a steady-state window it creates
`tserver_sim_num_tables` YCQL tables of `tserver_sim_tablets_per_table` tablets, one after another,
and prints a JSON report: registration time, heartbeat round trips with and without DDL running,
and `CreateTable` to `IsCreateTableDone` time per table.

Fake replicas cannot share Raft groups with real tservers, so measure a master with no real
tservers and the load balancer off. `run_baseline.sh` does that for a fresh single master:

```bash
src/yb/rust/master/tools/tserver-sim/run_baseline.sh build/latest [master binary] -- \
  --tserver_sim_num_tservers=100 --tserver_sim_steady_state_sec=20 \
  --tserver_sim_num_tables=50 --tserver_sim_tablets_per_table=8
```

Baseline, C++ master (the release before the switch), release build, 8-core dev server, RF 3, heartbeat interval 1 s:

| Tservers | Tables x tablets | Heartbeat p50 / p99, idle (ms) | Heartbeat p50 / p99, during DDL (ms) | CreateTable p50 / p99 (ms) |
|---|---|---|---|---|
| 100 (3 runs) | 50 x 8 | 0.88-1.08 / 1.43-1.60 | 1.16-1.24 / 3.8-4.5 | 240-242 / 259-1015 |
| 500 | 100 x 8 | 2.09 / 6.05 | 2.78 / 8.71 | 164 / 1023 |

Registration takes about two heartbeat intervals at both sizes: the simulator spreads the first
heartbeats over one interval, and the master answers a new tserver's first heartbeat with a
TryAgain that carries the universe UUID, so registration happens one interval later. These numbers measure the C++ master; layer 2
runs the same workload against the Rust leader.

The fake tservers echo the live tserver list version like real ones;
`--tserver_sim_send_live_tservers_version=false` makes them act like tservers of an earlier
release, which always get the full list.


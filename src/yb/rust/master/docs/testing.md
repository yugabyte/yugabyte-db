# Testing the Rust master

## Rust unit tests

`./yb_build.sh <type> --target rust_checks` runs `cargo fmt --check`, `cargo clippy -D warnings` and
`cargo test` for the workspace. `master-server`, `master-tserver-sim-main` and `yb-cpp-host` are excluded
from `cargo test`: their code calls into the C++ host, so it only links inside a CMake-built
executable and is covered by C++ tests.

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

`src/yb/integration-tests/rust_master-itest.cc` runs the Rust master in a quorum of C++ masters
(ExternalMiniCluster, YSQL off; skipped when `yb-master-rs` is not built):

```bash
./yb_build.sh release --cxx-test rust_master-itest --gtest_filter RustMasterTest.FollowerKeepsSameCatalog
```

| Test | Checks |
|---|---|
| `FollowerKeepsSameCatalog` | A C++ follower restarted as Rust on its own data stays a follower through YCQL DDL, an index, a snapshot schedule and a PITR restore; serves `GetMasterRegistration` and `ListMasters`; answers `NOT_THE_LEADER` to leader-only methods. With the cluster stopped, its sys-catalog rows and `auto_flags_config` equal a C++ follower's, and all rows pass the conformance check. |
| `NeverLeads` | A leader transfer to the Rust master ends with a C++ leader; the Rust master never reports LEADER. |
| `AddedThroughRemoteBootstrap` | A shell-mode Rust master is added with `ChangeConfig`, remote-bootstraps the sys catalog, follows a workload, and is removed. |
| `WebPages` | On the Rust follower's web port, the master list, version, leader-fetched JSON and HTML pages equal a C++ follower's (uptimes and the build-time footer aside); `/masters` bolds the Rust master; `is-leader` is 503; `/varz` and `/prometheus-metrics` come through the proxy; the registration advertises the Rust port. |
| `WebServerHttps` | With the webserver TLS flags the Rust master serves HTTPS with HSTS, refuses plain HTTP, fetches leader pages from an HTTP leader, and advertises the configured interface. |
| `TserverSimulator` | `master-tserver-sim` registers ten fake tservers with the leader of a quorum that has a Rust follower, and the leader lists them. |
| `FollowerCatalogMatchesRandomWorkload` | The Rust follower's in-memory catalog (`MasterRustPeer.DumpCatalog`) equals the C++ leader's sys catalog row for row after seeded random YCQL DDL, a snapshot schedule and a PITR restore; the restore reloaded the catalog and the self-check never disagreed. The seed is logged. |
| `RustLeaderServesCoreRpcs` | With `TEST_rust_master_allow_leadership`, the Rust master takes leadership; real tservers register and report to it; it serves table locations with leaders, `IsMasterLeaderServiceReady`, a cluster config change, and `NotSupported` for DDL. After failing back, the C++ leader reads the change, and every master's rows match. |
| `WarmFailover` | Two Rust masters: the leader's location stream reaches the other, which, after a failover with every tserver stopped, serves non-stale locations and the tserver list. |
| `LiveTserverListVersion` | Tservers echo the live-list version and the Rust leader answers "unchanged"; after a tserver stops, the others get the new list and keep heartbeating. |
| `CreatesUniverseLikeCpp` | A quorum of Rust masters creates a universe whose default rows equal those of a universe C++ masters created (random ids, uuids and the password salt aside), and every master holds them. |

### Running existing tests against Rust masters

`ExternalMiniClusterOptions::rust_masters` starts masters as `yb-master-rs` with
`--TEST_rust_master_allow_leadership`: `kAll` for every master, `kFirst` for one Rust master among
C++ ones. Its default comes from `YB_RUST_MASTER` (`1` or `all`, `mixed`), so an existing
ExternalMiniCluster test runs against Rust masters with, for example:

```bash
YB_RUST_MASTER=1 ./yb_build.sh release --cxx-test master_failover-itest
```

A test that starts masters with an explicit binary keeps it.

`MiniClusterOptions::rust_masters` does the same for MiniCluster tests, with the same
`YB_RUST_MASTER` default: MiniMaster runs those masters in the test process (see
[cpp-host.md](cpp-host.md#embedded-in-a-test-process)). A Rust MiniMaster has no C++ `Master`:
`master()` is null, and accessors of C++ master objects (`catalog_manager()`, `ts_manager()`,
`flush_manager()`) fail the test at once rather than skipping it, so the tests that still need
the C++ master show up. MiniCluster's own helpers (leader lookup, `WaitForTabletServerCount`,
`WaitForReplicaCount`) go through RPCs for Rust masters. `rust_master_mini_cluster-test` checks
leadership, a step-down and a follower restart with three Rust masters, and YCQL keyspace and table
DDL served by them: a table's tablets are created on every replica and serve reads and writes
before and after a leader change, the new leader alters and drops it, and a transactional table
gets `system.transactions` created first.

Java tests read the same variable: `MiniYBCluster` starts those masters as `yb-master-rs`.

### Tests that wait on W14

These read the C++ master's objects, so they run on Rust masters only after W14 ports them to
RPCs. The second column covers the same area on Rust masters until then.

| Tests | Covered on Rust masters by |
|---|---|
| `CqlTest.TestTruncateTableWithIndexes` | `CqlTest.TestTruncateTable` |
| `AutoFlagsMiniClusterTest.*` (they crash on the null `master()`) | `AutoFlagsExternalMiniClusterTest.*` |
| `PgDdlSavepointMiniClusterTest.*` (reads the C++ DDL verifier state) | `a_rollback_to_a_savepoint_undoes_only_later_ddl` (`master-table-lifecycle`), `PgDdlTransactionTest.*` |
| MiniCluster YSQL tests whose helpers call `catalog_manager()` or `master()` (`PgMiniTestBase::RestartMaster`) | `pg_libpq-test`, `pg_catalog_version-test` |

`WaitForInitDb` asks `IsInitDbDone` over RPC on Rust masters, so `PgMiniTestBase` suites that use
no other C++ accessor (`pg_advisory_lock-test`, `pg_auto_analyze-test`) run on Rust masters.

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

Baseline, C++ `yb-master`, release build, 8-core dev server, RF 3, heartbeat interval 1 s:

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


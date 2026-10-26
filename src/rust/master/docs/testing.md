# Testing the Rust master

## Rust unit tests

`./yb_build.sh <type> --target rust_checks` runs `cargo fmt --check`, `cargo clippy -D warnings` and
`cargo test` for the workspace. `master-server`, `master-tserver-sim-main` and `yb-cpp-host` are excluded
from `cargo test`: their code calls into the C++ host, so it only links inside a CMake-built
executable and is covered by C++ tests.

## Sys-catalog row conformance

The Rust master must serialize every sys-catalog row exactly as the C++ master does (see
[src/rust/docs/proto.md](../../docs/proto.md)). To check a real catalog:

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
(ExternalMiniCluster; skipped when `yb-master-rs` is not built):

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
src/rust/master/tools/tserver-sim/run_baseline.sh build/latest [master binary] -- \
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


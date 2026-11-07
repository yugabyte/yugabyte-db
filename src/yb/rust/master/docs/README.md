# The Rust master

The Rust master replaces the C++ master in `src/yb/master` one layer at a time on the
`feature-stack/hari-yb-master-rust` branch, and lands on main in one merge that also deletes the
C++ master. Until then the branch builds both: the C++ `yb-master` and the Rust `yb-master-rs`.

The sys-catalog tablet (Raft, WAL, DocDB) stays the shared C++ engine, hosted by
`src/yb/master_host/`. Rust owns the master logic above it.

The Rust master joins a quorum of C++ masters, on an existing master's data or by remote
bootstrap from shell mode, applies everything they replicate, and answers the RPCs a C++ follower
answers. Every Rust master keeps an in-memory catalog built from what its sys-catalog tablet
applies, the same way on leader and follower.

Layer 2 adds the core of a leader: the catalog writer, leadership, the tserver registry,
heartbeats and tablet reports, and the core cluster and client RPCs. A layer-2 leader serves no
DDL, snapshots, load balancing or YSQL, so leading needs `--TEST_rust_master_allow_leadership`;
without it the Rust master never leads ([leadership.md](leadership.md)).

| Doc | Covers |
|---|---|
| [cpp-host.md](cpp-host.md) | The master's C++ host and its bridges |
| [sys-catalog.md](sys-catalog.md) | Scans, Raft writes and the apply stream |
| [catalog-entity.md](catalog-entity.md) | Rows and in-memory entities |
| [catalog-state.md](catalog-state.md) | The in-memory catalog: materializer, writer, self-check |
| [leadership.md](leadership.md) | Leader phases, the fencing token, readiness |
| [ts-manager.md](ts-manager.md) | Tserver registry, heartbeats, tablet reports, the live tserver list |
| [tablet-locations.md](tablet-locations.md) | Observed replica locations and the location stream |
| [table-lifecycle.md](table-lifecycle.md) | Namespace and table DDL, tablet assignment |
| [cluster-balancer.md](cluster-balancer.md) | The cluster balancer: planner, controller, RPCs |
| [tablet-splits.md](tablet-splits.md) | Tablet splits |
| [task-runtime.md](task-runtime.md) | Scopes, controllers, retries, blocking pools |
| [web.md](web.md) | The web port and pages |
| [testing.md](testing.md) | Unit tests, conformance against the C++ master, itests |

Shared Rust pieces (build, bridges, protobuf, flags) are documented in
[src/yb/rust/docs](../../docs/README.md).

## Crates

| Crate | Owns |
|---|---|
| `master-server` | The static library `yb-master-rs` links: flag registration, runtime, the host-facing trait implementations, wiring |
| `master-rpc-handlers` | Master RPC handlers: the methods every master serves (some through the host), the core leader methods, `NOT_THE_LEADER` or `NotSupported` for the rest |
| `master-catalog-entity` | Sys-catalog rows by `SysRowEntryType` and the in-memory entity |
| `master-sys-catalog` | `SysCatalogStore`: scans, Raft writes, the apply stream; `FakeSysCatalog` |
| `master-catalog-state` | The in-memory catalog: snapshots, materializer, writer, self-check |
| `master-cluster-balancer` | The cluster balancer: a pure planner (a view of the cluster in, replica and leader moves out) and the controller that sends its moves |
| `master-tablet-split-manager` | Tablet splits: the split RPCs, the children's registration, the controller that sends splits |
| `master-leadership` | Leader phases and `LeaderToken` |
| `master-task-runtime` | Term and entity scopes, controllers, retries, blocking pools |
| `master-table-lifecycle` | Namespace and table DDL on the leader, and the controllers that create tablets |
| `master-tablet-locations` | Observed replica locations |
| `master-ts-manager` | Tserver registry, `TSHeartbeat`, tablet reports, the location stream |
| `tools/conformance` (`master-conformance`) | Checks Rust re-serialization of real rows against C++ |
| `tools/tserver-sim` (`master-tserver-sim`) | Fake tservers that load a master: registration, heartbeats, tablet reports, tablet RPCs |
| `tools/tserver-sim-main` | The static library of the `master-tserver-sim` executable: flags and the C++ host bridge |

Dependencies point down: `rpc-handlers` -> `ts-manager` -> `tablet-locations`, `catalog-state` ->
`leadership` -> `task-runtime`, and `catalog-state` -> `sys-catalog`, `catalog-entity`. Only
`master-server` touches the bridges; every other crate tests against fakes with `cargo test`.

## RPCs a Rust leader serves

| Service | Served | Later |
|---|---|---|
| MasterHeartbeat | TSHeartbeat ([ts-manager.md](ts-manager.md)) | |
| MasterCluster | ListTabletServers, ListLiveTabletServers, RemoveTabletServer, IsMasterLeaderServiceReady, GetMasterClusterConfig, ChangeMasterClusterConfig, SetPreferredZones, GetMasterXClusterConfig, GetAutoFlagsConfig, ValidateAutoFlagsConfig, IsLoadBalancerIdle, IsLoadBalanced, GetLoadMoveCompletion, GetLeaderBlacklistCompletion, AreLeadersOnPreferredOnly ([cluster-balancer.md](cluster-balancer.md)); ChangeLoadBalancerState and GetLoadBalancerState on every master | AutoFlags promote, rollback, single promote and demote (W9); DumpState |
| MasterClient | GetTabletLocations, GetTableLocations, GetTransactionStatusTablets (global table) | GetTabletsMetadata, GetStatefulServiceLocation, GetIndexBackfillProgress (W7); GetYsqlCatalogConfig, ReservePgsqlOids, GetYsqlYbSystemTableInfo (W9); Redis methods never (D16) |
| MasterDdl | CreateNamespace, DeleteNamespace, IsCreateNamespaceDone, IsDeleteNamespaceDone, ListNamespaces, GetNamespaceInfo, ListTables, GetTableSchema, CreateTable, IsCreateTableDone, AlterTable, IsAlterTableDone, DeleteTable and IsDeleteTableDone for YCQL tables that are not indexes ([table-lifecycle.md](table-lifecycle.md)) | The rest of W7 |
| MasterAdmin | SplitTablet (manual), DeleteNotServingTablet ([tablet-splits.md](tablet-splits.md)) | The rest of the admin service |
| MasterYsqlLease | | RefreshYsqlLease (W9) |
| MasterEncryption | GetFullUniverseKeyRegistry, IsEncryptionEnabled; GetUniverseKeyRegistry, AddUniverseKeys and HasUniverseKeyInMemory on every master | ChangeEncryptionInfo |
| MasterRustPeer | PushLocations, DumpCatalog (tests), on every Rust master | |

Every other leader method answers `NotSupported` on a ready Rust leader, naming its workstream.

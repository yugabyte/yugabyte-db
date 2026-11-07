# YSQL on the leader (`master-ysql-manager`)

What C++'s `YsqlManager`, `YsqlCatalogConfig` and `ObjectLockInfoManager` do for YSQL on a Rust
leader (W9).

## initdb

A Rust leader does not run initdb (`master_auto_run_initdb`). As a C++ master does, the first
leader of a new YSQL universe restores the shipped initial sys catalog snapshot
(`share/initial_sys_catalog_snapshot`), in its ready hook before it writes the default rows:

1. If the YSQL catalog config row says initdb is not done, the leader asks the host
   (`RestoreInitialSysCatalogSnapshot`). The host checks what
   `MaybeRestoreInitialSysCatalogSnapshotAndReloadSysCatalog` checks (`enable_ysql`, the
   snapshot path, `create_initial_sys_catalog_snapshot`, the instance metadata's
   `initdb_done_set_after_sys_catalog_restore`, `master_join_existing_universe`), submits the
   `RESTORE_ON_TABLET` operation, replays the snapshot's tablet metadata changes, and posts a
   catalog reload, since the restore bypasses the apply tap.
2. Once the catalog has reloaded, the default rows are written with `after_initial_snapshot`: the
   snapshot's cluster config, xCluster config and persistence block map belong to the universe
   that made it and are replaced, and `initdb_done` is set, all in the one default-rows write.

A crash between the two leaves initdb not done, so the next leader restores again, as in C++.
Unlike C++, a catalog with a YCQL keyspace outside the system ones is never restored: universes a
Rust leader created before this existed have no `initdb_done`, and the restore would wipe them.

`IsInitDbDone` answers from the YSQL catalog config row.

`yb-master-rs` calls `SetDefaultInitialSysCatalogSnapshotFlags` at startup, as `yb-master` does,
so the snapshot path defaults to the one shipped with the binary.

---
title: Distributed snapshots for YSQL
headerTitle: Distributed snapshots for YSQL
linkTitle: Distributed snapshots
description: Distributed snapshots for YSQL.
image: /images/section_icons/manage/enterprise.png
menu:
  preview:
    identifier: snapshots-1-ysql
    parent: backup-restore
    weight: 704
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../snapshot-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../snapshots-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

The most efficient way to back up the data stored in YugabyteDB is to create a distributed snapshot. A snapshot is a consistent cut of a data taken across all the nodes in the cluster. For YSQL, snapshots are created on per-database level. Backing up individual tables is currently not supported.

When YugabyteDB creates a snapshot, it doesn't physically copy the data; instead, it creates hard links to all the relevant files. These links reside on the same storage volumes where the data itself is stored, which makes both backup and restore operations nearly instantaneous.

{{< note title="Note on space consumption" >}}

There are no technical limitations on how many snapshots you can create. However, increasing the number of snapshots stored also increases the amount of space required for the database. The actual overhead depends on the workload, but you can estimate it by running tests based on your applications.

{{< /note >}}

## Create a snapshot

The distributed snapshots feature allows you to back up a database, and then restore it in case of a software or operational error, with minimal RTO and overhead.

To back up a database, create a snapshot using the [`create_database_snapshot`](../../../admin/yb-admin/#create-database-snapshot) command:

```sh
yb-admin create_database_snapshot my_database
```

When you run the command, it returns a unique ID for the snapshot:

```output
Started snapshot creation: 0d4b4935-2c95-4523-95ab-9ead1e95e794
```

You can then use this ID to check the status of the snapshot, [delete it](#delete-a-snapshot), or use it to [restore the database](#restore-a-snapshot).

The `create_database_snapshot` command exits immediately, but the snapshot may take some time to complete. Before using the snapshot, verify its status with the [`list_snapshots`](../../../admin/yb-admin/#list-snapshots) command:

```sh
yb-admin list_snapshots
```

This command lists the snapshots in the cluster, along with their states. Locate the ID of the new snapshot and make sure its state is COMPLETE:

```output
Snapshot UUID                         State
0d4b4935-2c95-4523-95ab-9ead1e95e794  COMPLETE
```

## Delete a snapshot

Snapshots never expire and are retained as long as the cluster exists. If you no longer need a snapshot, you can delete it with the [`delete_snapshot`](../../../admin/yb-admin/#delete-snapshot) command:

```sh
yb-admin delete_snapshot 0d4b4935-2c95-4523-95ab-9ead1e95e794
```

## Restore a snapshot

To restore the data backed up in one of the previously created snapshots, run the [`restore_snapshot`](../../../admin/yb-admin/#restore-snapshot) command:

```sh
yb-admin restore_snapshot 0d4b4935-2c95-4523-95ab-9ead1e95e794
```

This command rolls back the database to the state which it had when the snapshot was created. The restore happens in-place: in other words, it changes the state of the existing database within the same cluster.

## Move a snapshot to external storage

Storing snapshots in-cluster is extremely efficient, but also comes with downsides. It can increase the cost of the cluster by inflating the space consumption on the storage volumes. Also, in-cluster snapshots don't protect you from a disaster scenario like filesystem corruption or a hardware failure.

To mitigate these issues, consider storing backups outside of the cluster, in cheaper storage that is also geographically separated from the cluster. This approach helps you to reduce the cost, and also to restore your databases into a different cluster, potentially in a different location.

To move a snapshot to external storage, gather all the relevant files from all the nodes, and copy them along with the additional metadata required for restores on a different cluster:

1. Get the current YSQL schema catalog version by running the [`ysql_catalog_version`](../../../admin/yb-admin/#ysql-catalog-version) command:

    ```sh
    yb-admin ysql_catalog_version
    ```

    ```output
    Version: 1
    ```

1. [Create an in-cluster snapshot](#create-a-snapshot).

1. Back up the YSQL metadata using the [`ysql_dump`](../../../admin/ysql-dump) command:

    ```sh
    ysql_dump --include-yb-metadata --serializable-deferrable --create --schema-only --dbname my_database --file my_database_schema.sql
    ```

1. Verify that the catalog version is the same as it was prior to creating the snapshot. If it isn't, you're not guaranteed to get a consistent restorable snapshot, and should restart the process.

    ```sh
    yb-admin ysql_catalog_version
    ```

1. Create the snapshot metadata file by running the [`export_snapshot`](../../../admin/yb-admin/#export-snapshot) command and providing the ID of the snapshot:

    ```sh
    yb-admin export_snapshot 0d4b4935-2c95-4523-95ab-9ead1e95e794 my_database.snapshot
    ```

1. Copy the newly created YSQL metadata file (`my_database_schema.sql`) and the snapshot metadata file (`my_database.snapshot`) to the external storage.

1. Copy the tablet snapshot data into the external storage directory. Do this for all tablets of all tables in the database.

    ```sh
    cp -r ~/yugabyte-data/node-1/disk-1/yb-data/tserver/data/rocksdb/table-00004000000030008000000000004003/tablet-b0de9bc6a4cb46d4aaacf4a03bcaf6be.snapshots snapshot/
    ```

    The file path structure is:

    ```output
    <yb_data_dir>/node-<node_number>/disk-<disk_number>/yb-data/tserver/data/rocksdb/table-<table_id>/[tablet-<tablet_id>.snapshots]/<snapshot_id>
    ```

    - `<yb_data_dir>` is the directory where YugabyteDB data is stored. The default value is `~/yugabyte-data`.
    - `<node_number>` is used when multiple nodes are running on the same server (for testing, QA, and development). The default value is `1`.
    - `<disk_number>` when running YugabyteDB on multiple disks with the `--fs_data_dirs` flag. The default value is `1`.
    - `<table_id>` is the UUID of the table. You can get it from the `http://<yb-master-ip>:7000/tables` url in the Admin UI.
    - `<tablet_id>` in each table there is a list of tablets. Each tablet has a `<tablet_id>.snapshots` directory that you need to copy.
    - `<snapshot_id>` there is a directory for each snapshot since you can have multiple completed snapshots on each server.

    In practice, for each server, you will use the `--fs_data_dirs` flag, which is a comma-separated list of paths where to put the data (normally different paths should be on different disks).

    {{< note title="Tip" >}}

To get a snapshot of a multi-node cluster, you need to go into each node and copy
the folders of ONLY the leader tablets on that node. Because each tablet-replica has a copy of the same data, you do not need to keep a copy for each replica.

    {{< /note >}}

1. If you don't want to keep the in-cluster snapshot, it's now safe to [delete it](#delete-a-snapshot).

## Restore a snapshot from external storage

To restore a snapshot that you've [moved to external storage](#move-a-snapshot-to-external-storage), do the following:

1. Fetch the YSQL metadata file from the external storage and apply it using the [`ysqlsh`](../../../admin/ycqlsh/) CLI tool:

    ```sh
    ysqlsh -h 127.0.0.1 --echo-all --file=my_database_schema.sql
    ```

1. Fetch the snapshot metadata file from the external storage and apply it by running the [`import_snapshot`](../../../admin/yb-admin/#import-snapshot) command:

    ```sh
    yb-admin import_snapshot my_database.snapshot my_database
    ```

    The output contains the mapping between the old tablet IDs and the new tablet IDs:

    ```output
    Read snapshot meta file my_database.snapshot
    Importing snapshot 0d4b4935-2c95-4523-95ab-9ead1e95e794 (COMPLETE)
    Table type: table
    Target imported table name: test.t1
    Table being imported: test.t1
    Table type: table
    Target imported table name: test.t2
    Table being imported: test.t2
    Successfully applied snapshot.
    Object           Old ID                                 New ID
    Keyspace         00004000000030008000000000000000       00004000000030008000000000000000
    Table            00004000000030008000000000004003       00004000000030008000000000004001
    Tablet 0         b0de9bc6a4cb46d4aaacf4a03bcaf6be       50046f422aa6450ca82538e919581048
    Tablet 1         27ce76cade8e4894a4f7ffa154b33c3b       111ab9d046d449d995ee9759bf32e028
    Snapshot         0d4b4935-2c95-4523-95ab-9ead1e95e794   6beb9c0e-52ea-4f61-89bd-c160ec02c729
    ```

1. Copy the tablet snapshots.

    Use the tablet mappings to copy the tablet snapshot files from the external storage to the appropriate location.

    ```sh
    yb-data/tserver/data/rocksdb/table-<tableid>/tablet-<tabletid>.snapshots
    ```

    In our example, it'll be:

    ```sh
    cp -r snapshot/tablet-b0de9bc6a4cb46d4aaacf4a03bcaf6be.snapshots/0d4b4935-2c95-4523-95ab-9ead1e95e794 \
        ~/yugabyte-data-restore/node-1/disk-1/yb-data/tserver/data/rocksdb/table-00004000000030008000000000004001/tablet-50046f422aa6450ca82538e919581048.snapshots/6beb9c0e-52ea-4f61-89bd-c160ec02c729
    ```

    ```sh
    cp -r snapshot/tablet-27ce76cade8e4894a4f7ffa154b33c3b.snapshots/0d4b4935-2c95-4523-95ab-9ead1e95e794 \
        ~/yugabyte-data-restore/node-1/disk-1/yb-data/tserver/data/rocksdb/table-00004000000030008000000000004001/tablet-111ab9d046d449d995ee9759bf32e028.snapshots/6beb9c0e-52ea-4f61-89bd-c160ec02c729
    ```

    {{< note title="Note" >}}

For each tablet, you need to copy the snapshots folder on all tablet peers and in any configured read replica cluster.

    {{< /note >}}

1. [Restore the snapshot](#restore-a-snapshot).


{{< note title="Automated backups for YugabyteDB Anywhere" >}}

YugabyteDB Anywhere provides an API and UI for [backup and restore](../../../yugabyte-platform/back-up-restore-universes/), which automate most of the steps described here. You should use one or both, especially if you have many databases and snapshots to manage.

{{< /note >}}

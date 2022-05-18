---
title: Distributed Snapshots for YSQL
headerTitle: Distributed Snapshots for YSQL
linkTitle: Distributed Snapshots
description: Distributed Snapshots for YSQL.
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

The most efficient way to backup the data stored in YugabyteDB is to create a distributed snapshot. A snapshot is a consistent cut of a database taken across all the nodes in the cluster.

When YugabyteDB creates a snapshot, it doesn't physically copy the data, but instead creates hard links to all the relavant files. These links reside on the same storage volumes where the data itself is stored, which makes both backup and restore operations nearly instantanious.

{{< note title="Note on space consumption" >}}

There are no technical limitations on how many snapshots you can create. However, it's important to keep in mind that by increasing the number of snapshots stored, you also increase the amount of space required for the database. The actual overhead depends on the workload, so we recommend to estimate it by running tests based on your applications.

{{< /note >}}

## Creating a Snapshot

The distributed snapshots feature allows you to backup a database, and then restore it in case of a software or operational error, with minimal RTO and overhead.

To backup a database, create a snapshot using the [`create_database_snapshot`](../../../admin/yb-admin/#create-database-snapshot) command:

```sh
yb-admin create_database_snapshot my_database
```

When the command is executed, it generates a unique ID for the snapshot, and prints it out:

```output
Started snapshot creation: 0d4b4935-2c95-4523-95ab-9ead1e95e794
```

You can then use this ID to check the status of the snapshot, [delete it](#deleting-a-snapshot), or use it to [restore the database](#restoring-a-snapshot).

The `create_database_snapshot` command exits immidiately, so its completion does not necessarily means that the snapshot is successfully created. Before using the snapshot, you should verify its status by running the [`list_snapshots`](../../../admin/yb-admin/#list-snapshots) command:

```sh
yb-admin list_snapshots
```

This command will print out all the snapshots that exist in the cluster with their statuses. Locate the ID of the new snapshot and make sure its status is `COMPLETE`:

```output
Snapshot UUID                     State
0d4b49352c95452395ab9ead1e95e794  COMPLETE
```

## Deleting a Snapshot

Snapshots created using the `create_database_snapshot` never expire and are retained as long as the cluster exists. If a snapshot is no longer needed, you can delete it by running the [`delete_snapshot`](../../../admin/yb-admin/#delete-snapshot) command and providing the ID of the snapshot:

```sh
yb-admin delete_snapshot 0d4b4935-2c95-4523-95ab-9ead1e95e794
```

## Restoring a Snapshot

To restore a database to a previously created snapshot, run the [`restore_snapshot`](../../../admin/yb-admin/#restore-snapshot) command and provide the ID of the snapshot:

```sh
yb-admin restore_snapshot 0d4b4935-2c95-4523-95ab-9ead1e95e794
```

The above command will rollback the database to the state which it had when the snapshot was created. The restore happens in-place, i.e. it changes the state of the existing database within the same cluster.

## Moving a Snapshot to an External Storage

Storing snapshots in-cluster is extermely efficient, but also comes with downsides. First of all, it increases the cost of the cluster - increasing number of snapshot can inflate the space consumption on the storage volumes. Second of all, in-cluster snapshots do not protect you from disaster scenarios like filesystem corruption or hardware failures.

To mitigate the above, you might want to store backups outside of the cluster, in a cheaper storage that is also geografically separated from the cluster. This way, you can reduce the cost, and also restore you databases into a different cluster, potentially in a different location.

To move a snapshot to an external storage, you need to gather all the relevant files from all the nodes, and copy then along with additional metadata that will be required when you decide to restore the snapshot on a different cluster. Below is the detailed step-by-step explanation of the process.

1. Get the current YSQL schema catalog version by running the [`ysql_catalog_version`](../../../admin/yb-admin/#ysql-catalog-version) command:

    ```sh
    yb-admin ysql_catalog_version
    ```

    ```output
    Version:1
    ```

2. [Create an in-cluster snapshot](#creating-a-snapshot).

3. Create a backup of the YSQL metadata using the [`ysql_dump`](../../../admin/ysql-dump) command:

    ```sh
    ysql_dump --include-yb-metadata --serializable-deferrable --create --schema-only --dbname my_database --file my_database_schema.sql
    ```

4. Repeat step #1 and verify that the catalog version is the same as it was prior to creating the snapshot. If it is not, you're not guaranteed to get a consistent restorable snapshot, and should restart the process.

5. Create the snapshot metadata file by running the [`export_snapshot`](../../../admin/yb-admin/#export-snapshot) command and providing the ID of the snapshot:

    ```sh
    yb-admin export_snapshot 0d4b4935-2c95-4523-95ab-9ead1e95e794 my_database.snapshot
    ```

6. Copy the newly created YSQL metadata file (`my_database_schema.sql`) and the snapshot metadata file (`my_database.snapshot`) to the external storage.

7. Copy the tablet snapshot data into the external storage directory. Do this for all tablets of all tables in the database.

    ```sh
    cp -r ~/yugabyte-data/node-1/disk-1/yb-data/tserver/data/rocksdb/table-00004000000030008000000000004003/tablet-b0de9bc6a4cb46d4aaacf4a03bcaf6be.snapshots snapshot/
    ```

    The file path structure is:

    ```output
    <yb_data_dir>/node-<node_number>/disk-<disk_number>/yb-data/tserver/data/rocksdb/table-<table_id>/[tablet-<tablet_id>.snapshots]/<snapshot_id>
    ```

    - `<yb_data_dir>` is the directory where YugabyteDB data is stored. Default is `~/yugabyte-data`.
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

8. If you don't want to keep the in-cluster snapshot, it's now safe to [delete it](#deleting-a-snapshot).

## Restoring a Snapshot from an External Storage

To restore a snapshot that had been [moved to an external storage](#moving-a-snapshot-to-an-external-storage), go through the steps below.

1. Fetch the YSQL metadata file from the external storage and apply it using the [`ysqlsh`](../../../admin/ycqlsh/) CLI tool:

    ```sh
    ysqlsh -h 127.0.0.1 --echo-all --file=my_database_schema.sql
    ```

2. Fetch the snapshot metadata file from the external storage and apply it by running the [`import_snapshot`](../../../admin/yb-admin/#import-snapshot) command:

    ```sh
    yb-admin import_snapshot my_database.snapshot my_database
    ```

    The output will contain the mapping between the old tablet IDs and the new tablet IDs:

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

3. Copy the tablet snapshots.

    Use the tablet mappings to copy the tablet snapshot files from the external location to appropriate location.

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

4. [Restore the snapshot](restoring-a-snapshot).

-----

{{< note title="YugabyteDB Anywhere Automated Backups" >}}

YugabyteDB Anywhere provides the API and UI for [Backup and Restore](../../../yugabyte-platform/back-up-restore-universes/), which automates most of the steps described above. Consider using it for better usability, especially if you have many databases and snapshots to manage.

{{< /note >}}

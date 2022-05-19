---
title: Distributed Snapshots for YCQL
headerTitle: Distributed Snapshots for YCQL
linkTitle: Distributed Snapshots
description: Distributed Snapshots for YCQL.
image: /images/section_icons/manage/enterprise.png
menu:
  preview:
    identifier: snapshots-2-ycql
    parent: backup-restore
    weight: 705
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../snapshot-ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../snapshots-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

The most efficient way to backup the data stored in YugabyteDB is to create a distributed snapshot. A snapshot is a consistent cut of a data taken across all the nodes in the cluster. For YCQL, you can create a snapshot for either a whole keyspace, or for a single table including secondary indexes associated with this table.

When YugabyteDB creates a snapshot, it doesn't physically copy the data, but instead creates hard links to all the relavant files. These links reside on the same storage volumes where the data itself is stored, which makes both backup and restore operations nearly instantanious.

{{< note title="Note on space consumption" >}}

There are no technical limitations on how many snapshots you can create. However, it's important to keep in mind that by increasing the number of snapshots stored, you also increase the amount of space required for the database. The actual overhead depends on the workload, so we recommend to estimate it by running tests based on your applications.

{{< /note >}}

## Creating a Snapshot

The distributed snapshots feature allows you to backup a keyspace or a table, and then restore it in case of a software or operational error, with minimal RTO and overhead.

To backup a keyspace with all its tables and indexes, create a snapshot using the [`create_keyspace_snapshot`](../../../admin/yb-admin/#create-keyspace-snapshot) command:

```sh
yb-admin create_keyspace_snapshot my_keyspace
```

If you want to backup a single table with its indexes, use the [`create_snapshot`](../../../admin/yb-admin/#create-snapshot) command instead:

```sh
yb-admin create_snapshot my_keyspace my_table
```

When either of the above commands is invoked, YugabyteDB generates the unique ID for the newly created snapshot and prints it out:

```output
Started snapshot creation: a9442525-c7a2-42c8-8d2e-658060028f0e
```

You can then use this ID to check the status of the snapshot, [delete it](#deleting-a-snapshot), or use it to [restore the data](#restoring-a-snapshot).

Both `create_keyspace_snapshot` and `create_snapshot` commands exit immidiately, so the completion does not necessarily means that the snapshot is successfully created. Before using the snapshot, you should verify its status by running the [`list_snapshots`](../../../admin/yb-admin/#list-snapshots) command:

```sh
yb-admin list_snapshots
```

This command will print out all the snapshots that exist in the cluster with their statuses. Locate the ID of the new snapshot and make sure its status is `COMPLETE`:

```output
Snapshot UUID                         State
a9442525-c7a2-42c8-8d2e-658060028f0e  COMPLETE
```

## Deleting a Snapshot

Snapshots never expire and are retained as long as the cluster exists. If a snapshot is no longer needed, you can delete it by running the [`delete_snapshot`](../../../admin/yb-admin/#delete-snapshot) command and providing the ID of the snapshot:

```sh
yb-admin delete_snapshot a9442525-c7a2-42c8-8d2e-658060028f0e
```

## Restoring a Snapshot

To restore the data backed up in one of the previously created snapshots, run the [`restore_snapshot`](../../../admin/yb-admin/#restore-snapshot) command and provide the ID of the snapshot:

```sh
yb-admin restore_snapshot a9442525-c7a2-42c8-8d2e-658060028f0e
```

The above command will rollback the keyspace or the table to the state which it had when the snapshot was created. The restore happens in-place, i.e. it changes the state of the existing keyspace within the same cluster.

## Moving a Snapshot to an External Storage

Storing snapshots in-cluster is extermely efficient, but also comes with downsides. First of all, it increases the cost of the cluster - increasing number of snapshot can inflate the space consumption on the storage volumes. Second of all, in-cluster snapshots do not protect you from disaster scenarios like filesystem corruption or hardware failures.

To mitigate the above, you might want to store backups outside of the cluster, in a cheaper storage that is also geografically separated from the cluster. This way, you can reduce the cost, and also restore your data into a different cluster, potentially in a different location.

To move a snapshot to an external storage, you need to gather all the relevant files from all the nodes, and copy then along with additional metadata that will be required when you decide to restore the snapshot on a different cluster. Below is the detailed step-by-step explanation of the process.

1. [Create an in-cluster snapshot](#creating-a-snapshot).

2. Create the snapshot metadata file by running the [`export_snapshot`](../../../admin/yb-admin/#export-snapshot) command and providing the ID of the snapshot:

    ```sh
    yb-admin export_snapshot a9442525-c7a2-42c8-8d2e-658060028f0e my_keyspace.snapshot
    ```

3. Copy the newly created snapshot metadata file (`my_keyspace.snapshot`) to the external storage. The file path structure is:

    ```sh
    <yb_data_dir>/node-<node_number>/disk-<disk_number>/yb-data/tserver/data/rocksdb/table-<table_id>/[tablet-<tablet_id>.snapshots]/<snapshot_id>
    ```

    * `<yb_data_dir>` is the directory where YugabyteDB data is stored. (default=`~/yugabyte-data`)
    * `<node_number>` is used when multiple nodes are running on the same server (for testing, QA, and development). The default value is `1`.
    * `<disk_number>` when running yugabyte on multiple disks with the `--fs_data_dirs` flag. The default value is `1`.
    * `<table_id>` is the UUID of the table. You can get it from the `http://<yb-master-ip>:7000/tables` url in the Admin UI.
    * `<tablet_id>` in each table there is a list of tablets. Each tablet has a `<tablet_id>.snapshots` directory that you need to copy.
    * `<snapshot_id>` there is a directory for each snapshot since you can have multiple completed snapshots on each server.

    This directory structure is specific to `yb-ctl`, which is a local testing tool.
    In practice, for each server, you will use the `--fs_data_dirs` flag, which is a comma-separated list of paths where to put the data (normally different paths should be on different disks).
    In this `yb-ctl` example, these are the full paths up to the `disk-x`.

    {{< note >}}

To get a snapshot of a multi-node cluster, you need to go into each node and copy the folders of ONLY the leader tablets on that node. There is no need to keep a copy for each replica, since each tablet-replica has a copy of the same data.

  {{< /note >}}

4. If you don't want to keep the in-cluster snapshot, it's now safe to [delete it](#deleting-a-snapshot).

## Restoring a Snapshot from an External Storage

To restore a snapshot that had been [moved to an external storage](#moving-a-snapshot-to-an-external-storage), go through the steps below.

1. Fetch the snapshot metadata file from the external storage and apply it by running the [`import_snapshot`](../../../admin/yb-admin/#import-snapshot) command:

    ```sh
    yb-admin import_snapshot my_keyspace.snapshot my_keyspace
    ```

    The output will contain the mapping between the old tablet IDs and the new tablet IDs:

    ```output
    Read snapshot meta file my_keyspace.snapshot
    Importing snapshot a9442525-c7a2-42c8-8d2e-658060028f0e (COMPLETE)
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
    Snapshot         a9442525-c7a2-42c8-8d2e-658060028f0e   a9442525-c7a2-42c8-8d2e-658060028f0e
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

4. [Restore the snapshot](#restoring-a-snapshot).

-----

{{< note title="YugabyteDB Anywhere Automated Backups" >}}

YugabyteDB Anywhere provides the API and UI for [Backup and Restore](../../../yugabyte-platform/back-up-restore-universes/), which automates most of the steps described above. Consider using it for better usability, especially if you have many databases and snapshots to manage.

{{< /note >}}

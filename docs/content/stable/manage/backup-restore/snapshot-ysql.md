---
title: Distributed snapshots for YSQL
headerTitle: Distributed snapshots for YSQL
linkTitle: Distributed snapshots
description: Distributed snapshots for YSQL.
image: /images/section_icons/manage/enterprise.png
menu:
  stable:
    identifier: snapshots-1-ysql
    parent: backup-restore
    weight: 704
type: docs
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

When YugabyteDB creates a snapshot, it does not physically copy the data; instead, it creates hard links to all the relevant files. These links reside on the same storage volumes where the data itself is stored, which makes both backup and restore operations nearly instantaneous.

Note that even though there are no technical limitations on the number of snapshots you can create, increasing the number of stored snapshots also increases the amount of space required for the database. The actual overhead depends on the workload, but you can estimate it by running tests based on your applications.

## Create a snapshot

Using distributed snapshots allows you to back up a database and then restore it in case of a software or operational error, with minimal recovery time objectives (RTO) and overhead.

To back up a database, create a snapshot using the [`create_database_snapshot`](../../../admin/yb-admin/#create-database-snapshot) command, as follows:

```sh
./bin/yb-admin -master_addresses <ip1:7100,ip2:7100,ip3:7100> create_database_snapshot ysql.<database_name>
```

A unique ID for the snapshot is returned, as shown in the following sample output:

```output
Started snapshot creation: 0d4b4935-2c95-4523-95ab-9ead1e95e794
```

You can then use this ID to check the status of the snapshot, [delete it](#delete-a-snapshot), or use it to [restore the database](#restore-a-snapshot).

The `create_database_snapshot` command exits immediately, but the snapshot may take some time to complete. Before using the snapshot, verify its status by executing the [`list_snapshots`](../../../admin/yb-admin/#list-snapshots) command, as follows:

```sh
./bin/yb-admin -master_addresses <ip1:7100,ip2:7100,ip3:7100> list_snapshots
```

All the snapshots in the cluster are listed, along with their statuses. You can find the ID of the new snapshot and make sure it has been completed, as shown in the following  sample output:

```output
Snapshot UUID                           State       Creation Time
0d4b4935-2c95-4523-95ab-9ead1e95e794    COMPLETE    2023-04-20 00:20:38.214201
```

## Delete a snapshot

Snapshots never expire and are retained as long as the cluster exists. If you no longer need a snapshot, you can delete it by executing the [`delete_snapshot`](../../../admin/yb-admin/#delete-snapshot) command, as follows:

```sh
./bin/yb-admin -master_addresses <ip1:7100,ip2:7100,ip3:7100> delete_snapshot 0d4b4935-2c95-4523-95ab-9ead1e95e794
```

## Restore a snapshot

To restore the data backed up in one of the previously created snapshots, run the [`restore_snapshot`](../../../admin/yb-admin/#restore-snapshot) command, as follows:

```sh
./bin/yb-admin -master_addresses <ip1:7100,ip2:7100,ip3:7100> restore_snapshot 0d4b4935-2c95-4523-95ab-9ead1e95e794
```

This command rolls back the database to the state which it had when the snapshot was created. The restore happens in-place: it changes the state of the existing database in the same cluster.

Note that the described in-cluster workflow only reverts data changes, but not schema changes. For example, if you create a snapshot, drop a table, and then restore the snapshot, the table is not restored. As a workaround, you can either [store snapshots outside of the cluster](#move-a-snapshot-to-external-storage) or use [point-in-time recovery](../../../manage/backup-restore/point-in-time-recovery/). This limitation will be removed in an upcoming release. For more information, see the tracking issue [12977](https://github.com/yugabyte/yugabyte-db/issues/12977).

## Move a snapshot to external storage

Even though storing snapshots in-cluster is extremely efficient, it can increase the cost of the cluster by inflating the space consumption on the storage volumes. In addition, in-cluster snapshots do not provide protection from file system corruption or a hardware failure.

To mitigate these issues, consider storing backups outside of the cluster, in cheaper storage that is also geographically separated from the cluster. This approach would not only allow you to reduce the cost, but restore your databases into a different cluster, potentially in a different location.

To move a snapshot to external storage, gather all the relevant files from all the nodes and copy them along with the additional metadata required for restoring on a different cluster, as follows:

1. Obtain the current YSQL schema catalog version by running the following query in [ysqlsh](../../../admin/ysqlsh/):

    ```sql
    SELECT yb_catalog_version();
    ```

    The following is a sample output:

    ```output
     yb_catalog_version 
    --------------------
                    13
    ```

1. [Create an in-cluster snapshot](#create-a-snapshot).

1. Back up the YSQL metadata using the [`ysql_dump`](../../../admin/ysql-dump) command, as follows:

    ```sh
    ./postgres/bin/ysql_dump -h <ip> --include-yb-metadata --serializable-deferrable --create --schema-only --dbname <database_name> --file <database_name>_schema.sql
    ```

1. Using ysqlsh, verify that the catalog version is the same as it was prior to creating the snapshot, as follows:

    ```sql
    SELECT yb_catalog_version();
    ```

    If the catalog version is not the same, you are not guaranteed to get a consistent restorable snapshot and you should restart the process.

1. Create the snapshot metadata file by executing the [`export_snapshot`](../../../admin/yb-admin/#export-snapshot) command and providing the ID of the snapshot, as follows:

    ```sh
    ./bin/yb-admin -master_addresses <ip1:7100,ip2:7100,ip3:7100> export_snapshot 0d4b4935-2c95-4523-95ab-9ead1e95e794 <database_name>.snapshot
    ```

1. Copy the newly-created YSQL metadata file (`<database_name>_schema.sql`) and the snapshot metadata file (`<database_name>.snapshot`) to the external storage.

1. Copy the tablet snapshot data into the external storage directory. Do this for all tablets of all tables in the database, as follows:

    ```sh
    cp -r ~/yugabyte-data/node-1/disk-1/yb-data/tserver/data/rocksdb/table-00004000000030008000000000004003/tablet-b0de9bc6a4cb46d4aaacf4a03bcaf6be.snapshots/snapshot_id/
    ```

    The following is the file path structure:

    ```output
    <yb_data_dir>/node-<node_number>/disk-<disk_number>/yb-data/tserver/data/rocksdb/table-<table_id>/[tablet-<tablet_id>.snapshots]/<snapshot_id>
    ```

    - *<yb_data_dir>* - a directory where YugabyteDB data is stored. The default value is `~/yugabyte-data`.
    - *<node_number>* - used when multiple nodes are running on the same server (for testing, QA, and development). The default value is `1`.
    - *<disk_number>* - used when running YugabyteDB on multiple disks with the `--fs_data_dirs` flag. The default value is `1`.
    - *<table_id>* - the UUID of the table. You can obtain it from the `http://<yb-master-ip>:7000/tables` URL in the Admin UI.
    - *<tablet_id>* - each table contains a list of tablets. Each tablet has a `<tablet_id>.snapshots` directory that you need to copy.
    - *<snapshot_id>* - there is a directory for each snapshot, as you can have multiple completed snapshots on each server.

    In practice, for each server, you would use the `--fs_data_dirs` flag, which is a comma-separated list of paths for the data. It is recommended to have different paths on separate disks.

To obtain a snapshot of a multi-node cluster, you would access each node and copy the folders of only the leader tablets on that node. Because each tablet replica has a copy of the same data, there is no need to keep a copy for each replica.

If you do not wish to keep the in-cluster snapshot, you can safely [delete it](#delete-a-snapshot).

## Restore a snapshot from external storage

You can restore a snapshot that you have [moved to external storage](#move-a-snapshot-to-external-storage), as follows:

1. Make sure that the database you are going to restore doesn't exist, and drop it if it does:

    ```sql
    DROP DATABASE IF EXISTS <database_name>;
    ```

1. Retrieve the YSQL metadata file from the external storage and apply it using the [`ysqlsh`](../../../admin/ycqlsh/) tool by executing the following command:

    ```sh
    ./bin/ysqlsh -h 127.0.0.1 --echo-all --file=<database_name>_schema.sql
    ```

1. Fetch the snapshot metadata file from the external storage and apply it by running the [`import_snapshot`](../../../admin/yb-admin/#import-snapshot) command, as follows:

    ```sh
    ./bin/yb-admin -master_addresses <ip1:7100,ip2:7100,ip3:7100> import_snapshot <database_name>.snapshot <database_name>
    ```

    Notice that the following output contains the mapping between the old tablet IDs and the new tablet IDs:

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

1. Copy the tablet snapshots. Use the tablet mappings to copy the tablet snapshot files from the external storage to the appropriate location such as `yb-data/tserver/data/rocksdb/table-<tableid>/tablet-<tabletid>.snapshots`.<br>

    Based on the preceding examples, you would execute the following commands:

    ```sh
    scp -r /mnt/d0/yb-data/tserver/data/rocksdb/table-00004000000030008000000000004003/ \
        tablet-b0de9bc6a4cb46d4aaacf4a03bcaf6be.snapshots/0d4b4935-2c95-4523-95ab-9ead1e95e794/* \
        <target_node_ip>:/mnt/d0/yb-data/tserver/data/rocksdb/table-00004000000030008000000000004001/ \
        tablet-50046f422aa6450ca82538e919581048.snapshots/6beb9c0e-52ea-4f61-89bd-c160ec02c729/
    ```

    ```sh
    scp -r /mnt/d0/yb-data/tserver/data/rocksdb/table-00004000000030008000000000004003/ \
        tablet-27ce76cade8e4894a4f7ffa154b33c3b.snapshots/0d4b4935-2c95-4523-95ab-9ead1e95e794/* \
        <target_node_ip>:/mnt/d0/yb-data/tserver/data/rocksdb/table-00004000000030008000000000004001/ \
        tablet-111ab9d046d449d995ee9759bf32e028.snapshots/6beb9c0e-52ea-4f61-89bd-c160ec02c729/
    ```

    For each tablet, you need to copy only the contents of the snapshots folder (not the entire folder) on all tablet peers, and in any configured read replica cluster.

1. [Restore the snapshot](#restore-a-snapshot).

{{< note title="Note" >}}

YugabyteDB Anywhere provides an API and UI for [backup and restore](../../../yugabyte-platform/back-up-restore-universes/), automating most of the steps. You should use one or both, especially if you have many databases and snapshots to manage.

{{< /note >}}

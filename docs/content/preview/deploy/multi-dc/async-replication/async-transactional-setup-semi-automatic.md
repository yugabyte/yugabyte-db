---
title: Deploy transactional xCluster
headerTitle: Deploy transactional xCluster
linkTitle: Deploy
description: Setting up transactional (active-active single-master) replication between two YB universes
headContent: Set up transactional xCluster replication
menu:
  preview:
    parent: async-replication-transactional
    identifier: async-transactional-setup-2-semi-automatic
    weight: 10
tags:
  other: ysql
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../async-transactional-setup-automatic/" class="nav-link">
      Automatic
    </a>
  </li>
  <li >
    <a href="../async-transactional-setup-semi-automatic/" class="nav-link active">
      Semi-Automatic
    </a>
  </li>
  <li >
    <a href="../async-transactional-setup-manual/" class="nav-link">
      Manual
    </a>
  </li>
</ul>

{{< note title="Note" >}}
To use semi-automatic transactional xCluster replication, both the Primary and Standby universes must be running v2024.1.2 or later.
{{< /note >}}

Semi-automatic transactional xCluster replication simplifies the operational complexity of managing replication and making DDL changes.

In this mode, xCluster replication operates at the YSQL database granularity. This means you only run xCluster management operations when adding and removing databases from replication, and not when tables in the databases are created or dropped.

In particular, [DDL changes](#making-ddl-changes) don't require the use of yb-admin. This means DDL changes can be made by any database administrator or user with database permissions, and don't require SSH access or intervention by an IT administrator.

## Set up replication

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#yugabyted-setup" class="nav-link active" id="yugabyted-setup-tab" data-bs-toggle="tab"
      role="tab" aria-controls="yugabyted-setup" aria-selected="true">
      <img src="/icons/database.svg" alt="Server Icon">
      Yugabyted
    </a>
  </li>
  <li>
    <a href="#local-setup" class="nav-link" id="local-setup-tab" data-bs-toggle="tab"
      role="tab" aria-controls="local-setup" aria-selected="false">
      <i class="icon-shell"></i>
      Local
    </a>
  </li>
</ul>
<div class="tab-content">
  <div id="yugabyted-setup" class="tab-pane fade show active" role="tabpanel" aria-labelledby="yugabyted-setup-tab">

<!-- YugabyteD Setup -->

The following assumes you have set up Primary and Standby universes. Refer to [Set up yugabyted universes](../../../../reference/configuration/yugabyted/#start). The yugabyted node must be started with `--backup_daemon=true` to initialize the backup/restore agent.


1. Create a checkpoint on the Primary universe for all the databases that you want to be part of the replication.

    ```sh
    ./bin/yugabyted xcluster create_checkpoint \
        --replication_id <replication_id> \
        --databases <comma_separated_database_names>
    ```

    The command informs you if any data needs to be copied to the Standby, or only the schema (empty tables and indexes) needs to be created. For example:

    ```output
    +-------------------------------------------------------------------------+
    |                                yugabyted                                |
    +-------------------------------------------------------------------------+
    | Status               : xCluster create checkpoint success.              |
    | Bootstrapping        : Bootstrap is required for database `yugabyte`.   |
    +-------------------------------------------------------------------------+

    For each database which requires bootstrap run the following commands to perform a backup and restore.
    # Run on source:
    ./yugabyted backup --cloud_storage_uri <AWS/GCP/local cloud storage uri>  --database <database_name> --base_dir <base_dir of source node>
    # Run on target:
    ./yugabyted restore --cloud_storage_uri <AWS/GCP/local cloud storage uri>  --database <database_name> --base_dir <base_dir of target node>
    ```

1. If needed, perform a full copy of the database(s) on the Primary to the Standby using distributed [backup](../../../../reference/configuration/yugabyted/#backup) and [restore](../../../../reference/configuration/yugabyted/#restore). If your source database is not empty, it must be bootstrapped, even if the output suggests otherwise. This applies even if it contains only empty tables, unused types, or enums (#24030).

1. Enable [point in time restore (PITR)](../../../../manage/backup-restore/point-in-time-recovery/) on the database(s) on both the Primary and Standby universes:

    ```sh
    ./bin/yugabyted configure point_in_time_recovery \
        --enable \
        --retention <retention_period> \
        --database <database_name>
    ```

    The `retention_period` must be greater than the amount of time you expect the Primary universe to be down before it self recovers or before you perform a failover to the Standby universe.

1. Set up the xCluster replication.

    ```sh
    ./bin/yugabyted xcluster set_up \
        --target_address <ip_of_any_target_cluster_node> \
        --replication_id <replication_id> \
        --bootstrap_done
    ```

    You should see output similar to the following:

    ```output
    +-----------------------------------------------+
    |                   yugabyted                   |
    +-----------------------------------------------+
    | Status        : xCluster set-up successful.   |
    +-----------------------------------------------+

    ```
  </div>

  <div id="local-setup" class="tab-pane fade " role="tabpanel" aria-labelledby="local-setup-tab">

<!-- Local Setup -->
The following assumes you have set up Primary and Standby universes. Refer to [Set up universes](../async-deployment/#set-up-universes).

1. Create a checkpoint using the `create_xcluster_checkpoint` command, providing a name for the replication group, and the names of the databases to replicate as a comma-separated list.

    ```sh
    ./bin/yb-admin \
        -master_addresses <primary_master_addresses> \
        create_xcluster_checkpoint <replication_group_id> <comma_separated_namespace_names>
    ```

    The command informs you if any data needs to be copied to the Standby, or only the schema (empty tables and indexes) needs to be created. For example:

    ```output
    Waiting for checkpointing of database(s) to complete

    Checkpointing of yugabyte completed. Bootstrap is not required for setting up xCluster replication
    Successfully checkpointed databases for xCluster replication group repl_group1

    Create equivalent YSQL objects (schemas, tables, indexes, ...) for databases [yugabyte] on the standby universe
    Once the above step(s) complete run 'setup_xcluster_replication'
    ```

    You can also manually check the status as follows:

    ```sh
    ./bin/yb-admin \
    -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
    is_xcluster_bootstrap_required repl_group1 yugabyte
    ```

    You should see output similar to the following:

    ```output
    Waiting for checkpointing of database(s) to complete

    Checkpointing of yugabyte completed. Bootstrap is not required for setting up xCluster replication
    ```

1. If needed, perform a full copy of the database on the Primary to the Standby using distributed backup and restore. See [Distributed snapshots for YSQL](../../../../manage/backup-restore/snapshot-ysql/). Otherwise, create the necessary schema objects (tables and indexes) on the Standby.

1. Enable [point in time restore (PITR)](../../../../manage/backup-restore/point-in-time-recovery/) on the database(s) on both the Primary and Standby universes:

    ```sh
    ./bin/yb-admin \
        -master_addresses <standby_master_addresses> \
        create_snapshot_schedule 1 10 ysql.yugabyte
    ```

1. Set up the xCluster replication group.

    ```sh
    ./bin/yb-admin \
    -master_addresses <primary_master_addresses> \
    setup_xcluster_replication <replication_group_id> <standby_master_addresses>
    ```

    You should see output similar to the following:

    ```output
    xCluster Replication group repl_group1 setup successfully
    ```

  </div>
</div>

## Monitor replication
For information on monitoring xCluster replication, refer to [Monitor xCluster](../../../../launch-and-manage/monitor-and-alert/xcluster-monitor/).


## Add a database to a replication group

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#yugabyted-add-db" class="nav-link active" id="yugabyted-add-db-tab" data-bs-toggle="tab"
      role="tab" aria-controls="yugabyted-add-db" aria-selected="true">
      <img src="/icons/database.svg" alt="Server Icon">
      Yugabyted
    </a>
  </li>
  <li>
    <a href="#local-add-db" class="nav-link" id="local-add-db-tab" data-bs-toggle="tab"
      role="tab" aria-controls="local-add-db" aria-selected="false">
      <i class="icon-shell"></i>
      Local
    </a>
  </li>
</ul>
<div class="tab-content">
  <div id="yugabyted-add-db" class="tab-pane fade show active" role="tabpanel" aria-labelledby="yugabyted-add-db-tab">

<!-- YugabyteD Setup -->
  </div>

  <div id="local-add-db" class="tab-pane fade " role="tabpanel" aria-labelledby="local-add-db-tab">

<!-- Local Setup -->
The database should have at least one table in order to be added to replication. If it is a colocated database then there should be at least one colocated table in the database in order for it to be added to replication.

To add a database to replication, do the following:

1. Enable point in time restore (PITR) on the Standby database:

    ```sh
    ./bin/yb-admin \
        -master_addresses <standby_master_addresses> \
        create_snapshot_schedule 1 10 ysql.yugabyte
    ```

1. Create a checkpoint.

    ```sh
    ./bin/yb-admin \
    -master_addresses <primary_master_addresses> \
    add_namespace_to_xcluster_checkpoint <replication_group_id> <namespace_name>
    ```

    You should see output similar to the following:

    ```output
    Waiting for checkpointing of database to complete

    Successfully checkpointed database db2 for xCluster replication group repl_group1

    Bootstrap is not required for adding database to xCluster replication
    Create equivalent YSQL objects (schemas, tables, indexes, ...) for the database in the standby universe
    ```

1. Set up the database using the checkpoint.

    ```sh
    ./bin/yb-admin \
    -master_addresses <primary_master_addresses> \
    add_namespace_to_xcluster_replication <replication_group_id> <namespace_name> <standby_master_addresses>
    ```

    You should see output similar to the following:

    ```output
    Successfully added db2 to xCluster Replication group repl_group1
    ```

  </div>
</div>

## Remove a database from replication

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#yugabyted-remove-db" class="nav-link active" id="yugabyted-remove-db-tab" data-bs-toggle="tab"
      role="tab" aria-controls="yugabyted-remove-db" aria-selected="true">
      <img src="/icons/database.svg" alt="Server Icon">
      Yugabyted
    </a>
  </li>
  <li>
    <a href="#local-remove-db" class="nav-link" id="local-remove-db-tab" data-bs-toggle="tab"
      role="tab" aria-controls="local-remove-db" aria-selected="false">
      <i class="icon-shell"></i>
      Local
    </a>
  </li>
</ul>
<div class="tab-content">
  <div id="yugabyted-remove-db" class="tab-pane fade show active" role="tabpanel" aria-labelledby="yugabyted-remove-db-tab">

<!-- YugabyteD Setup -->
  </div>

  <div id="local-remove-db" class="tab-pane fade " role="tabpanel" aria-labelledby="local-remove-db-tab">

<!-- Local Setup -->
To remove a database from a replication group, use the following command:

```sh
./bin/yb-admin \
-master_addresses <primary_master_addresses> \
remove_namespace_from_xcluster_replication <replication_group_id> <namespace_name> <standby_master_addresses>
```

You should see output similar to the following:

```output
Successfully removed db2 from xCluster Replication group repl_group1
```

  </div>
</div>

## Drop xCluster replication group

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#yugabyted-drop" class="nav-link active" id="yugabyted-drop-tab" data-bs-toggle="tab"
      role="tab" aria-controls="yugabyted-drop" aria-selected="true">
      <img src="/icons/database.svg" alt="Server Icon">
      Yugabyted
    </a>
  </li>
  <li>
    <a href="#local-drop" class="nav-link" id="local-drop-tab" data-bs-toggle="tab"
      role="tab" aria-controls="local-drop" aria-selected="false">
      <i class="icon-shell"></i>
      Local
    </a>
  </li>
</ul>
<div class="tab-content">
  <div id="yugabyted-drop" class="tab-pane fade show active" role="tabpanel" aria-labelledby="yugabyted-drop-tab">

<!-- YugabyteD Setup -->
  </div>

  <div id="local-drop" class="tab-pane fade " role="tabpanel" aria-labelledby="local-drop-tab">

<!-- Local Setup -->
To drop a replication group, use the following command:

```sh
./bin/yb-admin \
-master_addresses <primary_master_addresses> \
drop_xcluster_replication <replication_group_id> <standby_master_addresses>
```

You should see output similar to the following:

```output
Outbound xCluster Replication group rg1 deleted successfully
```

  </div>
</div>

## Making DDL changes

When performing any DDL operation on databases using semi-automatic transactional xCluster replication (such as creating, altering, or dropping tables, indexes, or partitions), do the following:

1. Execute the DDL on Primary.
1. Execute the DDL on Standby.

The xCluster configuration is updated automatically. You can insert data into the table as soon as it is created on Primary.

When new tables are created with CREATE TABLE, CREATE INDEX, or CREATE TABLE PARTITION OF on the Primary universe, new streams are automatically created. Because this happens alongside the DDL, the new tables are checkpointed at the start of the WAL.

When the same DDL is run on the Standby, the stream info is automatically fetched from the Primary and the table is added to replication using the pre-created stream.

Similarly on table drop, after both sides drop the table, the streams are automatically removed.

When making DDL changes, keep in mind the following:

- DDLs have to be executed on the Standby universe in the same order in which they were executed on the Primary universe.
- When executing multiple schema changes for a given table, each DDL has to be run on both universes before the next DDL/schema change can be performed.

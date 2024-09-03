---
title: Set up transactional xCluster replication
headerTitle: Set up transactional xCluster replication
linkTitle: Set up replication
description: Setting up transactional (active-standby) replication between universes
headContent: Set up semi-automatic transactional replication
menu:
  preview:
    parent: async-replication-transactional
    identifier: async-transactional-setup-1-db
    weight: 10
badges: ysql
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../async-transactional-setup-dblevel/" class="nav-link active">
      <i class="icon-shell"></i>
      Semi-Automatic
    </a>
  </li>
  <li >
    <a href="../async-transactional-setup/" class="nav-link">
      <i class="icon-shell"></i>
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

The following assumes you have set up Primary and Standby universes. Refer to [Set up universes](../async-deployment/#set-up-universes).

## Set up replication

To set up unidirectional transactional replication, do the following:

1. Enable point in time restore (PITR) on the Standby database:

    ```sh
    ./bin/yb-admin \
        -master_addresses <standby_master_addresses> \
        create_snapshot_schedule 1 10 ysql.yugabyte
    ```

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

1. If needed, perform a full copy of the database on the Primary to the Standby using distributed backup and restore. See [Distributed snapshots for YSQL](../../../manage/backup-restore/snapshot-ysql/). Otherwise, create the necessary schema objects (tables and indexes) on the Standby.

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

### List replication groups

To list outgoing groups on the Primary universe, enter the following command:

```sh
./bin/yb-admin \
-master_addresses <primary_master_addresses> \
list_xcluster_outbound_replication_groups [namespace_id]
```

You should see output similar to the following:

```output
1 Outbound Replication Groups found: 
[repl_group1]
```

To list inbound groups on the Standby universe, enter the following command:

```sh
./bin/yb-admin \
-master_addresses <standby_master_addresses> \
list_universe_replications [namespace_id]
```

You should see output similar to the following:

```output
1 Universe Replication Groups found: 
[repl_group1]
```

## Add a database to a replication group

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

### Remove a database from replication

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

## Drop xCluster replication group

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

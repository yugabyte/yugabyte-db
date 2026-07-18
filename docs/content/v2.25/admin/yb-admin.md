---
title: yb-admin - command line tool for advanced YugabyteDB administration
headerTitle: yb-admin
linkTitle: yb-admin
description: Use the yb-admin command line tool for advanced administration of YugabyteDB clusters.
menu:
  v2.25:
    identifier: yb-admin
    parent: admin
    weight: 30
type: docs
---

The yb-admin utility, located in the `bin` directory of YugabyteDB home, provides a command line interface for administering clusters.

It invokes the [yb-master](../../reference/configuration/yb-master/) and [yb-tserver](../../reference/configuration/yb-tserver/) servers to perform the necessary administration.

{{< note title="Using YugabyteDB Anywhere or YugabyteDB Aeon?" >}}

yb-admin is intended to be used to administer manually created and managed universes only.

If you are using [YugabyteDB Anywhere](../../yugabyte-platform/) or [YugabyteDB Aeon](/preview/yugabyte-cloud/), administer your universes using the respective UI, or, to use automation, use the respective API or CLI. For more information, refer to [YugabyteDB Anywhere automation](../../yugabyte-platform/anywhere-automation/) and [YugabyteDB Aeon automation](/preview/yugabyte-cloud/managed-automation/).

**If you perform tasks on a YugabyteDB Anywhere-managed universe using yb-admin, the changes may not be reflected in YugabyteDB Anywhere.**

If you are unsure whether a particular functionality in yb-admin is available in YugabyteDB Anywhere, contact {{% support-platform %}}.

{{< /note >}}

## Syntax

To use yb-admin from the YugabyteDB home directory, run `./bin/yb-admin` using the following syntax.

```sh
yb-admin \
    [ --master_addresses <master-addresses> ]  \
    [ --init_master_addrs <master-address> ]  \
    [ --timeout_ms <millisec> ] \
    [ --certs_dir_name <dir-name> ] \
    <command> [ command_flags ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* `init_master_addrs`: Allows specifying a single YB-Master address from which the rest of the YB-Masters are discovered.
* `timeout_ms`: The RPC timeout, in milliseconds. Default 60000. A value of 0 means don't wait; -1 means wait indefinitely.
* `certs_dir_name`: The directory with certificates to use for secure server connections. Default is `""`.

  To connect to a cluster with TLS enabled, you must include the `-certs_dir_name` flag with the directory location where the root certificate is located.
* **command**: The operation to be performed. See [Commands](#commands) for syntax details and examples.
* **command_flags**: Configuration flags that can be applied to the command.

### Online help

To display the online help, run `yb-admin --help` from the YugabyteDB home directory.

```sh
./bin/yb-admin --help
```

## Commands

* [Universe and cluster](#universe-and-cluster-commands)
* [Table](#table-commands)
* [Backup and snapshot](#backup-and-snapshot-commands)
* [Deployment topology](#deployment-topology-commands)
  * [Multi-zone and multi-region](#multi-zone-and-multi-region-deployment-commands)
  * [Read replica](#read-replica-deployment-commands)
* [Security](#security-commands)
  * [Encryption at rest](#encryption-at-rest-commands)
* [Change data capture (CDC)](#change-data-capture-cdc-commands)
* [xCluster replication](#xcluster-replication-commands)
* [Decommissioning](#decommissioning-commands)
* [Rebalancing](#rebalancing-commands)
* [Upgrade](#upgrade)

---

### Universe and cluster commands

#### get_universe_config

Gets the configuration for the universe.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    get_universe_config
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.

#### change_config

Changes the configuration of a tablet.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    change_config <tablet-id> \
    [ ADD_SERVER | REMOVE_SERVER ] \
    <peer-uuid> \
    [ PRE_VOTER | PRE_OBSERVER ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *tablet-id*: The identifier (ID) of the tablet.
* ADD_SERVER | REMOVE_SERVER: Subcommand to add or remove the server.
* *peer-uuid*: The UUID of the tablet server hosting the peer tablet.
* PRE_VOTER | PRE_OBSERVER: Role of the new peer joining the quorum. Required when using the ADD_SERVER subcommand.

**Notes:**

If you need to take a node down temporarily, but intend to bring it back up, you should not need to use the REMOVE_SERVER subcommand.

* If the node is down for less than 15 minutes, it will catch up through RPC calls when it comes back online.
* If the node is offline longer than 15 minutes, then it will go through Remote Bootstrap, where the current leader will forward all relevant files to catch up.

If you do not intend to bring a node back up (perhaps you brought it down for maintenance, but discovered that the disk is bad), then you want to decommission the node (using the REMOVE_SERVER subcommand) and then add in a new node (using the ADD_SERVER subcommand).

#### change_master_config

Changes the master configuration.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    change_master_config \
    [ ADD_SERVER|REMOVE_SERVER ] \
    <ip-addr> <port> \
    [<uuid>]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* ADD_SERVER | REMOVE_SERVER: Adds or removes a new YB-Master server.

  After adding or removing a node, verify the status of the YB-Master server on the YB-Master UI page (<http://node-ip:7000>) or run the yb-admin [dump_masters_state](#dump-masters-state) command.
* *ip-addr*: The IP address of the server node.
* *port*: The port of the server node.
* *uuid*: The UUID for the server that is being added/removed.

#### list_tablet_servers

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    list_tablet_servers <tablet-id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *tablet-id*: The identifier (ID) of the tablet.

#### list_tablets

Lists all tablets and their replica locations for a particular table.

Use this to find out who the LEADER of a tablet is.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    list_tablets <keyspace-type>.<keyspace-name> <table> [<max-tablets>]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *keyspace-type*: Type of the keyspace, ysql or ycql.
* *keyspace-name*: The namespace, or name of the database or keyspace.
* *table*: The name of the table.
* *max-tablets*: The maximum number of tables to be returned. Default is `10`. Set to `0` to return all tablets.

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    list_tablets ysql.db_name table_name 0
```

```output
Tablet UUID                       Range                                                     Leader
cea3aaac2f10460a880b0b4a2a4b652a  partition_key_start: "" partition_key_end: "\177\377"     127.0.0.1:9100
e509cf8eedba410ba3b60c7e9138d479  partition_key_start: "\177\377" partition_key_end: ""
```

#### list_all_tablet_servers

Lists all tablet servers.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    list_all_tablet_servers
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.

#### list_all_masters

Displays a list of all YB-Master servers in a table listing the master UUID, RPC host and port, state (`ALIVE` or `DEAD`), and role (`LEADER`, `FOLLOWER`, or `UNKNOWN_ROLE`).

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    list_all_masters
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.

**Example**

```sh
./bin/yb-admin \
    --master_addresses node7:7100,node8:7100,node9:7100 \
    list_all_masters
```

```output
Master UUID         RPC Host/Port          State      Role
...                   node8:7100           ALIVE     FOLLOWER
...                   node9:7100           ALIVE     FOLLOWER
...                   node7:7100           ALIVE     LEADER
```

#### list_replica_type_counts

Prints a list of replica types and counts for the specified table.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    list_replica_type_counts <keyspace> <table-name>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *keyspace*: The name of the database or keyspace.
* *table-name*: The name of the table.

#### dump_masters_state

Prints the status of the YB-Master servers.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    dump_masters_state
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.

#### list_tablet_server_log_locations

List the locations of the tablet server logs.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    list_tablet_server_log_locations
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.

#### list_tablets_for_tablet_server

Lists all tablets for the specified tablet server (YB-TServer).

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    list_tablets_for_tablet_server <ts-uuid>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *ts-uuid*: The UUID of the tablet server (YB-TServer).

#### split_tablet

Splits the specified hash-sharded tablet and computes the split point as the middle of tablet's sharding range.

```sh
yb-admin \
    --master_addresses <master-addresses> \
    split_tablet <tablet-id-to-split>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *tablet-id-to-split*: The identifier of the tablet to split.

For more information on tablet splitting, see:

* [Tablet splitting](../../architecture/docdb-sharding/tablet-splitting) — Architecture overview
* [Automatic Re-sharding of Data with Tablet Splitting](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/docdb-automatic-tablet-splitting.md) — Architecture design document in the GitHub repository.

#### master_leader_stepdown

Forces the master leader to step down. The specified YB-Master node will take its place as leader.

{{< note title="Note" >}}

* Use this command only if recommended by Yugabyte support.

* There is a possibility of downtime.

{{< /note >}}

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    master_leader_stepdown [ <new-leader-id> ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *new-leader-id*: (Optional) The identifier (ID) of the new YB-Master leader. If not specified, the new leader is automatically elected.

#### ysql_catalog_version

Prints the current YSQL schema catalog version.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    ysql_catalog_version
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    ysql_catalog_version
```

The version output displays:

```output
Version:1
```

---

### Table commands

#### list_tables

Prints a list of all tables. Optionally, include the database type, table ID, and the table type.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    list_tables \
    [ include_db_type ] [ include_table_id ] [ include_table_type ]
```

```sh
yb-admin \
    --master_addresses <master-addresses> list_tables
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* `include_db_type`: (Optional) Add this flag to include the database type for each table.
* `include_table_id`: (Optional) Add this flag to include the unique UUID associated with the table.
* `include_table_type`: (Optional) Add this flag to include the table type for each table.

Returns tables in the following format, depending on the flags used:

```output
<db-type>.<namespace>.<table-name> <table-id> <table-type>
```

* *db-type*: The type of database. Valid values are `ysql`, `ycql`, and `unknown`.
* *namespace*: The name of the database (for YSQL) or keyspace (for YCQL).
* *table-name*: The name of the table.
* *table-id*: The UUID of the table.
* *table-type*: The type of table. Valid values are `catalog`, `table`, `index`, and `other`.

{{< note title="Tip" >}}

To display a list of tables and their UUID (`table_id`) values, open the **YB-Master UI** (`<master_host>:7000/`) and click **Tables** in the navigation bar.

{{< /note >}}

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    list_tables
```

```output
...
yugabyte.pg_range
template1.pg_attrdef
template0.pg_attrdef_adrelid_adnum_index
template1.pg_conversion
system_platform.pg_opfamily
postgres.pg_opfamily_am_name_nsp_index
system_schema.functions
template0.pg_statistic
system.local
template1.pg_inherits_parent_index
template1.pg_amproc
system_platform.pg_rewrite
yugabyte.pg_ts_config_cfgname_index
template1.pg_trigger_tgconstraint_index
template1.pg_class
template1.pg_largeobject
system_platform.sql_parts
template1.pg_inherits
...
```

#### compact_table

Triggers manual compaction on a table.

**Syntax 1: Using table name**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    compact_table <db-type>.<namespace> <table> [<timeout-in-seconds>] [ADD_INDEXES]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *db-type*: The type of database. Valid values are `ysql` and `ycql`.
* *namespace*: The name of the database (for YSQL) or keyspace (for YCQL).
* *table*: The name of the table to compact.
* *timeout-in-seconds*: Specifies duration (in seconds) yb-admin waits for compaction to end. Default is `20`.
* ADD_INDEXES: Whether to compact the secondary indexes associated with the table. Default is `false`.

**Example**

```sh
./bin/yb-admin \
    --master_addresses $MASTER_RPC_ADDRS \
    compact_table ysql.yugabyte table_name
```

```output
Compacted [yugabyte.table_name] tables.
```

**Syntax 2: Using table ID**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    compact_table tableid.<table-id> [<timeout-in-seconds>] [ADD_INDEXES]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *table-id*: The unique UUID associated with the table.
* *timeout-in-seconds*: Specifies duration (in seconds) yb-admin waits for compaction to end. Default is `20`.
* ADD_INDEXES: Whether to compact the secondary indexes associated with the table. Default is `false`.

**Example**

```sh
./bin/yb-admin \
    --master_addresses $MASTER_RPC_ADDRS \
    compact_table tableid.000033eb000030008000000000004002
```

```output
Compacted [000033eb000030008000000000004002] tables.
```

#### compaction_status

Show the status of full compaction on a table.

```sh
yb-admin \
    --master_addresses <master-addresses> \
    compaction_status <db-type>.<namespace> <table> [show_tablets]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *db-type*: The type of database. Valid values are `ysql` and `ycql`.
* *namespace*: The name of the database (for YSQL) or keyspace (for YCQL).
* *table*: The name of the table to show the full compaction status.
* `show_tablets`: Show the compactions status of individual tablets.

**Example**

```sh
./bin/yb-admin \
    --master_addresses $MASTER_RPC_ADDRS \
    compaction_status ysql.yugabyte table_name show_tablets
```

```output
tserver uuid: 1b9486461cdd48f59eb46b33992cd73a
 tablet id | full compaction state | last full compaction completion time

 93c6933407e24adf8b3f12c11499673a IDLE 2025-06-03 15:36:22.395586
 9c1cddfe33ec440cbcd70770563c62ca IDLE 2025-06-03 15:36:22.743703
 b739745bba254330805c259459c61a7e IDLE 2025-06-03 15:36:23.416460
 9d0155aa77b3441e8e8c78cc433b995c IDLE 2025-06-03 15:36:23.504400
 2b9f283301d14be2add2c3f2a0016531 IDLE 2025-06-03 15:36:23.892202
 eff101a879f348778ed599cb79498c44 IDLE 2025-06-03 15:36:24.706769

tserver uuid: c0505f1d31774a3d88fae26ce14cde10
 tablet id | full compaction state | last full compaction completion time

 93c6933407e24adf8b3f12c11499673a IDLE 2025-06-03 15:36:22.769900
 9c1cddfe33ec440cbcd70770563c62ca IDLE 2025-06-03 15:36:23.142609
 b739745bba254330805c259459c61a7e IDLE 2025-06-03 15:36:23.871247
 9d0155aa77b3441e8e8c78cc433b995c IDLE 2025-06-03 15:36:23.877126
 2b9f283301d14be2add2c3f2a0016531 IDLE 2025-06-03 15:36:24.294265
 eff101a879f348778ed599cb79498c44 IDLE 2025-06-03 15:36:25.107964

tserver uuid: f7b5e6fc38974cbabc330d944d564974
 tablet id | full compaction state | last full compaction completion time

 93c6933407e24adf8b3f12c11499673a IDLE 2025-06-03 15:36:22.415413
 9c1cddfe33ec440cbcd70770563c62ca IDLE 2025-06-03 15:36:22.793145
 b739745bba254330805c259459c61a7e IDLE 2025-06-03 15:36:23.473077
 9d0155aa77b3441e8e8c78cc433b995c IDLE 2025-06-03 15:36:23.475270
 2b9f283301d14be2add2c3f2a0016531 IDLE 2025-06-03 15:36:23.888733
 eff101a879f348778ed599cb79498c44 IDLE 2025-06-03 15:36:24.705576

Last full compaction completion time: 2025-06-03 15:36:22.395586
Last admin compaction request time: 2025-06-03 15:36:22.061267
```

#### modify_table_placement_info

Modifies the placement information (cloud, region, and zone) for a table.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    modify_table_placement_info <keyspace> <table-name> <placement-info> <replication-factor> \
    [ <placement-id> ]
```

or alternatively:

```sh
yb-admin \
    --master_addresses <master-addresses> \
    modify_table_placement_info tableid.<table-id> <placement-info> <replication-factor> \
    [ <placement-id> ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *keyspace*: The namespace, or name of the database or keyspace.
* *table-name*: The name of the table.
* *table-id*: The unique UUID associated with the table whose placement policy is being changed.
* *placement-info*: Comma-delimited list of placements for *cloud*.*region*.*zone*. Default is `cloud1.datacenter1.rack1`.
* *replication-factor*: The number of replicas for each tablet.
* *placement-id*: Identifier of the primary cluster. Optional. If set, it has to match the placement ID specified for the primary cluster in the cluster configuration.

**Example**

```sh
./bin/yb-admin \
    --master_addresses $MASTER_RPC_ADDRS \
    modify_table_placement_info  testdatabase testtable \
    aws.us-west.us-west-2a,aws.us-west.us-west-2b,aws.us-west.us-west-2c 3
```

Verify this in the Master UI by opening the **YB-Master UI** (`<master_host>:7000/`) and clicking **Tables** in the navigation bar. Navigate to the appropriate table whose placement information you're changing, and check the Replication Info section.

{{< note title="Notes" >}}

Setting placement for tables is not supported for clusters with read-replicas or leader affinity policies enabled.

Use this command to create custom placement policies only for YCQL tables or transaction status tables. For YSQL tables, use [Tablespaces](../../explore/going-beyond-sql/tablespaces) instead.
{{< /note >}}

#### create_transaction_table

Creates a transaction status table to be used in a region. This command should always be followed by [modify_table_placement_info](#modify-table-placement-info) to set the placement information for the newly-created transaction status table.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    create_transaction_table \
    <table-name>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *table-name*: The name of the transaction status table to be created; this must start with `transactions_`.

The transaction status table will be created as `system.<table-name>`.

**Example**

```sh
./bin/yb-admin \
    --master_addresses $MASTER_RPC_ADDRS \
    create_transaction_table \
    transactions_us_east
```

Verify this in the Master UI by opening the **YB-Master UI** (`<master_host>:7000/`) and clicking **Tables** in the navigation bar. You should see a new system table with keyspace `system` and table name `transactions_us_east`.

Next, set the placement on the newly created transactions table:

```sh
./bin/yb-admin \
    --master_addresses $MASTER_RPC_ADDRS \
    modify_table_placement_info system transactions_us_east \
    aws.us-east.us-east-1a,aws.us-east.us-east-1b,aws.us-east.us-east-1c 3
```

After the load balancer runs, all tablets of `system.transactions_us_east` should now be solely located in the AWS us-east region.

{{< note title="Note" >}}

The preferred way to create transaction status tables with YSQL is to create a tablespace with the appropriate placement. YugabyteDB automatically creates a transaction table using the tablespace's placement when you create the first table using the new tablespace.

{{< /note >}}

#### add_transaction_tablet

Add a tablet to a transaction status table.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    add_transaction_tablet \
    <table-id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *table-id*: The unique UUID associated with the table to be compacted.

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    add_transaction_tablet 000033eb000030008000000000004002
```

To verify that the new status tablet has been created, run the [list_tablets](#list-tablets) command.

#### flush_table

Flush the memstores of the specified table on all tablet servers to disk.

**Syntax 1: Using table name**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    flush_table <db-type>.<namespace> <table> [<timeout-in-seconds>] [ADD_INDEXES]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *db-type*: The type of database. Valid values are `ysql` and `ycql`.
* *namespace*: The name of the database (for YSQL) or keyspace (for YCQL).
* *table*: The name of the table to flush.
* *timeout-in-seconds*: Specifies duration (in seconds) yb-admin waits for flushing to end. Default is `20`.
* ADD_INDEXES: Whether to flush the secondary indexes associated with the table. Default is `false`.

**Example**

```sh
./bin/yb-admin \
    --master_addresses $MASTER_RPC_ADDRS \
    flush_table ysql.yugabyte table_name

```

```output
Flushed [yugabyte.table_name] tables.
```

**Syntax 2: Using table ID**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    flush_table tableid.<table-id> [<timeout-in-seconds>] [ADD_INDEXES]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *table-id*: The unique UUID associated with the table.
* *timeout-in-seconds*: Specifies duration (in seconds) yb-admin waits for flushing to end. Default is `20`.
* ADD_INDEXES: Whether to flush the secondary indexes associated with the table. Default is `false`.

**Example**

```sh
./bin/yb-admin \
    --master_addresses $MASTER_RPC_ADDRS \
    flush_table tableid.000033eb000030008000000000004002
```

```output
Flushed [000033eb000030008000000000004002] tables.
```

#### backfill_indexes_for_table

Backfill all DEFERRED indexes in a YCQL table.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    backfill_indexes_for_table <keyspace> <table-name>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *keyspace*: Specifies the keyspace `ycql.keyspace-name`.
* *table-name*: Specifies the table name.

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    backfill_indexes_for_table ybdemo table_name
```

A new backfill job is created for all the `DEFERRED` indexes of the table. The command does not have any output.

---

### Backup and snapshot commands

The following backup and snapshot commands are available:

* [**create_database_snapshot**](#create-database-snapshot) creates a snapshot of the specified YSQL database
* [**create_keyspace_snapshot**](#create-keyspace-snapshot) creates a snapshot of the specified YCQL keyspace
* [**list_snapshots**](#list-snapshots) returns a list of all snapshots, restores, and their states
* [**create_snapshot**](#create-snapshot) creates a snapshot of one or more YCQL tables and indexes
* [**restore_snapshot**](#restore-snapshot) restores a snapshot
* [**list_snapshot_restorations**](#list-snapshot-restorations) returns a list of all snapshot restorations
* [**export_snapshot**](#export-snapshot) creates a snapshot metadata file
* [**import_snapshot**](#import-snapshot) imports a snapshot metadata file
* [**import_snapshot_selective**](#import-snapshot-selective) imports a specified snapshot metadata file
* [**delete_snapshot**](#delete-snapshot) deletes a snapshot's information
* [**create_snapshot_schedule**](#create-snapshot-schedule) sets the schedule for snapshot creation
* [**list_snapshot_schedules**](#list-snapshot-schedules) returns a list of all snapshot schedules
* [**edit_snapshot_schedule**](#edit-snapshot-schedule) modifies the schedule for snapshot creation
* [**restore_snapshot_schedule**](#restore-snapshot-schedule) restores all objects in a scheduled snapshot
* [**delete_snapshot_schedule**](#delete-snapshot-schedule) deletes the specified snapshot schedule

{{< note title="YugabyteDB Anywhere" >}}

If you are using YugabyteDB Anywhere to manage point-in-time-recovery (PITR) for a universe, you must initiate and manage PITR using the YugabyteDB Anywhere UI. If you use the yb-admin CLI to make changes to the PITR configuration of a universe managed by YugabyteDB Anywhere, including creating schedules and snapshots, your changes are not reflected in YugabyteDB Anywhere.

{{< /note >}}

#### create_database_snapshot

Creates a snapshot of the specified YSQL database.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    create_database_snapshot <database>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *database*: The name of the YSQL database.

When this command runs, a `snapshot_id` is generated and printed.

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    create_database_snapshot
```

To see if the database snapshot creation has completed, run the [yb-admin list_snapshots](#list-snapshots) command.

#### create_keyspace_snapshot

Creates a snapshot of the specified YCQL keyspace.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    create_keyspace_snapshot <keyspace>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *keyspace*: The name of the YCQL keyspace.

When this command runs, a `snapshot_id` is generated and printed.

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    create_keyspace_snapshot
```

To see if the database snapshot creation has completed, run the [yb-admin list_snapshots](#list-snapshots) command.

#### list_snapshots

Prints a list of all snapshot IDs, restoration IDs, and states. Optionally, prints details (including keyspaces, tables, and indexes) in JSON format.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    list_snapshots \
    [ show_details ] [ not_show_restored ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* `show_details`: (Optional) Print snapshot details, including the keyspaces, tables, and indexes.
* `not_show_restored`: (Optional) Do not show successful "restorations" (that is, `COMPLETE`). Use to see a list of only uncompleted or failed restore operations.
* `show_deleted`: (Optional) Show snapshots that are deleted, but still retained in memory.

Possible `state` values for creating and restoring snapshots:

* `create_snapshot`: `CREATING`, `COMPLETE`, `DELETING`, `DELETED`, or `FAILED`.
* `restore_snapshot`: `COMPLETE`, `DELETING`, `DELETED`, or `FAILED`.

By default, the `list_snapshots` command prints the current state of the following operations:

* `create_snapshot`: `snapshot_id`, `keyspace`, `table`,  `state`
* `restore_snapshot`: `snapshot_id`, `restoration_id`,  `state`.
* `delete_snapshot`: `snapshot_id`,  `state`.

When `show_details` is included, the `list_snapshots` command prints the following details in JSON format:

* `type`: `NAMESPACE`
  * `id`: `<snapshot_id>` or `<restoration_id>`
  * `data`:
    * `name`:  `"<namespace_name>"`
    * `database_type`: `"YQL_DATABASE_CQL"`
    * `colocated`: `true` or `false`
    * `state`: `"<state>"`
* `type`: `TABLE` <== Use for table or index
  * `id`: `"<table_id>"`  or `"<index_id>"`
  * `data`:
    * `name`: `"<table_name>"` or `"<index_id>"`
    * `version`: `"<table_version>"`
    * `state`: `"<state>"`
    * `state_msg`: `"<state_msg>"`
    * `next_column_id`: `"<column_id>"`
    * `table_type`: `"YQL_TABLE_TYPE"`
    * `namespace_id`: `"<namespace_id>"`
    * `indexed_table_id` (index only): `<table_id>`
    * `is_local_index` (index only): `true` or `false`
    * `is_unique_index` (index only):  `true` or `false`

**Example**

In this example, the optional `show_details` flag is added to generate the snapshot details.

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    list_snapshots show_details
```

Because `show_details` was included, `list_snapshots` prints the details in JSON format, like this:

```output
f566b03b-b85e-41a0-b903-875cd305c1c5  COMPLETE
{"type":"NAMESPACE","id":"8053dd55d478437cba57d9f67caac154","data":{"name":"yugabyte","database_type":"YQL_DATABASE_CQL","colocated":false,"state":"RUNNING"}}
{"type":"TABLE","id":"a7e940e724ef497ebe94bf69bfe507d9","data":{"name":"tracking1","version":1,"state":"RUNNING","state_msg":"Current schema version=1","next_column_id":13,"table_type":"YQL_TABLE_TYPE","namespace_id":"8053dd55d478437cba57d9f67caac154"}}
{"type":"NAMESPACE","id":"8053dd55d478437cba57d9f67caac154","data":{"name":"yugabyte","database_type":"YQL_DATABASE_CQL","colocated":false,"state":"RUNNING"}}
{"type":"TABLE","id":"b48f4d7695f0421e93386f7a97da4bac","data":{"name":"tracking1_v_idx","version":0,"state":"RUNNING","next_column_id":12,"table_type":"YQL_TABLE_TYPE","namespace_id":"8053dd55d478437cba57d9f67caac154","indexed_table_id":"a7e940e724ef497ebe94bf69bfe507d9","is_local_index":false,"is_unique_index":false}}
```

If `show_details` is not included, `list_snapshots` prints the `snapshot_id` and `state`:

```output
f566b03b-b85e-41a0-b903-875cd305c1c5  COMPLETE
```

#### create_snapshot

Creates a snapshot of the specified YCQL tables and their indexes. Prior to v.2.1.8, indexes were not automatically included. You can specify multiple tables, even from different keyspaces.

{{< note title="Snapshots don't auto-expire" >}}

Snapshots you create via `create_snapshot` persist on disk until you remove them using the [delete_snapshot](#delete-snapshot) command.

Use the [create_snapshot_schedule](#create-snapshot-schedule) command to create snapshots that expire after a specified time interval.

{{</ note >}}

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    create_snapshot <keyspace> <table-name> | <table_id> \
    [<keyspace> <table-name> | <table-id> ]... \
    [<flush-timeout-in-seconds>]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *keyspace*: The name of the database or keyspace formatted as <ycql|ysql>.<keyspace>.
* *table-name*: The name of the table name.
* *table-id*: The unique UUID associated with the table.
* *flush-timeout-in-seconds*: Specifies duration (in seconds) before flushing snapshot. Default is `60`. To skip flushing, set the value to `0`.

When this command runs, a `snapshot_id` is generated and printed.

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    create_snapshot ydb test_tb
```

```output
Started flushing table ydb.test_tb
Flush request id: fe0db953a7a5416c90f01b1e11a36d24
Waiting for flushing...
Flushing complete: SUCCESS
Started snapshot creation: 4963ed18fc1e4f1ba38c8fcf4058b295
```

To see if the snapshot creation has finished, run the [yb-admin list_snapshots](#list-snapshots) command.

#### restore_snapshot

Restores the specified snapshot, including the tables and indexes. When the operation starts, a `restoration_id` is generated.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    restore_snapshot <snapshot-id> <restore-target>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *snapshot-id*: The identifier (ID) for the snapshot.
* *restore-target*: The time to which to restore the snapshot. This can be either an absolute Unix time, or a relative time such as `minus 5m` (to restore to 5 minutes ago). Optional; omit to restore to the given snapshot's creation time.

**Example**

```sh
./bin/yb-admin restore_snapshot 72ad2eb1-65a2-4e88-a448-7ef4418bc469
```

When the restore starts, the `snapshot_id` and the generated `restoration_id` are displayed.

```output
Started restoring snapshot: 72ad2eb1-65a2-4e88-a448-7ef4418bc469
Restoration id: 5a9bc559-2155-4c38-ac8b-b6d0f7aa1af6
```

To see if the snapshot was successfully restored, you can run the [yb-admin list_snapshots](#list-snapshots) command.

```sh
./bin/yb-admin list_snapshots
```

For the example above, the restore failed, so the following displays:

```output
Restoration UUID                      State
5a9bc559-2155-4c38-ac8b-b6d0f7aa1af6  FAILED
```

#### list_snapshot_restorations

Lists the snapshots restorations.

Returns one or more restorations in JSON format.

**restorations list** entries contain:

* the restoration's unique ID
* the snapshot's unique ID
* state of the restoration

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    list_snapshot_restorations <restoration-id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *restoration-id*: The snapshot restoration's unique identifier. Optional; omit the ID to return all restorations in the system.

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    list_snapshot_restorations 26ed9053-0c26-4277-a2b8-c12d0fa4c8cf
```

```output.json
{
    "restorations": [
        {
            "id": "26ed9053-0c26-4277-a2b8-c12d0fa4c8cf",
            "snapshot_id": "ca8f3763-5437-4594-818d-713fb0cddb96",
            "state": "RESTORED"
        }
    ]
}
```

#### export_snapshot

Generates a metadata file for the specified snapshot, listing all the relevant internal UUIDs for various objects (table, tablet, etc.).

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    export_snapshot <snapshot-id> <file-name>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *snapshot-id*: The identifier (ID) for the snapshot.
* *file-name*: The name of the file to contain the metadata. Recommended file extension is `.snapshot`.

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    export_snapshot 4963ed18fc1e4f1ba38c8fcf4058b295 \
    test_tb.snapshot
```

```output
Exporting snapshot 4963ed18fc1e4f1ba38c8fcf4058b295 (COMPLETE) to file test_tb.snapshot
Snapshot meta data was saved into file: test_tb.snapshot
```

#### import_snapshot

Imports the specified snapshot metadata file.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    import_snapshot <file-name> \
    [<keyspace> <table-name> [<keyspace> <table-name>]...]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *file-name*: The name of the snapshot file to import
* *keyspace*: The name of the database or keyspace
* *table-name*: The name of the table

{{< note title="Note" >}}

The *keyspace* and the *table* can be different from the exported one.

{{< /note >}}

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    import_snapshot test_tb.snapshot ydb test_tb
```

```output
Read snapshot meta file test_tb.snapshot
Importing snapshot 4963ed18fc1e4f1ba38c8fcf4058b295 (COMPLETE)
Target imported table name: ydb.test_tb
Table being imported: ydb.test_tb
Successfully applied snapshot.
Object            Old ID                            New ID
Keyspace          c478ed4f570841489dd973aacf0b3799  c478ed4f570841489dd973aacf0b3799
Table             ff4389ee7a9d47ff897d3cec2f18f720  ff4389ee7a9d47ff897d3cec2f18f720
Tablet 0          cea3aaac2f10460a880b0b4a2a4b652a  cea3aaac2f10460a880b0b4a2a4b652a
Tablet 1          e509cf8eedba410ba3b60c7e9138d479  e509cf8eedba410ba3b60c7e9138d479
Snapshot          4963ed18fc1e4f1ba38c8fcf4058b295  4963ed18fc1e4f1ba38c8fcf4058b295
```

#### import_snapshot_selective

Imports only the specified tables from the specified snapshot metadata file.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    import_snapshot_selective <file-name> \
    [<keyspace> <table-name> [<keyspace> <table-name>]...]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *file-name*: The name of the snapshot file to import
* *keyspace*: The name of the database or keyspace
* *table-name*: The name of the table

{{< note title="Note" >}}

The *keyspace* can be different from the exported one. The name of the table needs to be the same.

{{< /note >}}

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    import_snapshot_selective test_tb.snapshot ydb test_tb
```

```output
Read snapshot meta file test_tb.snapshot
Importing snapshot 4963ed18fc1e4f1ba38c8fcf4058b295 (COMPLETE)
Target imported table name: ydb.test_tb
Table being imported: ydb.test_tb
Successfully applied snapshot.
Object            Old ID                            New ID
Keyspace          c478ed4f570841489dd973aacf0b3799  c478ed4f570841489dd973aacf0b3799
Table             ff4389ee7a9d47ff897d3cec2f18f720  ff4389ee7a9d47ff897d3cec2f18f720
Tablet 0          cea3aaac2f10460a880b0b4a2a4b652a  cea3aaac2f10460a880b0b4a2a4b652a
Tablet 1          e509cf8eedba410ba3b60c7e9138d479  e509cf8eedba410ba3b60c7e9138d479
Snapshot          4963ed18fc1e4f1ba38c8fcf4058b295  4963ed18fc1e4f1ba38c8fcf4058b295
```

#### delete_snapshot

Deletes the specified snapshot.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    delete_snapshot <snapshot-id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *snapshot-id*: The identifier (ID) of the snapshot.

#### create_snapshot_schedule

Creates a snapshot schedule. A schedule consists of a list of objects to be included in a snapshot, a time interval at which to take snapshots for them, and a retention time.

Returns a schedule ID in JSON format.

**Syntax**

```sh
yb-admin create_snapshot_schedule \
    --master_addresses <master-addresses> \
    <snapshot-interval>\
    <retention-time>\
    <filter-expression>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *snapshot-interval*: The frequency at which to take snapshots, in minutes.
* *retention-time*: The number of minutes to keep a snapshot before deleting it.
* *filter-expression*: The set of objects to include in the snapshot.

The filter expression is a list of acceptable objects, which can be either raw tables, keyspaces (YCQL) in the format `keyspace_name`, or databases (YSQL) in the format `ysql.database_name`. For proper consistency guarantees, set this up _per-keyspace_ (YCQL) or _per-database_ (YSQL).

**Example**

Take a snapshot of the YSQL database `yugabyte` once an hour, and retain each snapshot for 2 hours:

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    create_snapshot_schedule 60 120 ysql.yugabyte
```

The equivalent command for the YCQL keyspace `yugabyte` would be the following:

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    create_snapshot_schedule 60 120 yugabyte
```

```output.json
{
  "schedule_id": "6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256"
}
```

#### list_snapshot_schedules

Lists the snapshots associated with a given schedule. Or, lists all schedules and their associated snapshots.

Returns one or more schedule lists in JSON format.

**Schedule list** entries contain:

* schedule ID
* schedule options (interval and retention time)
* a list of snapshots that the system has automatically taken

**Snapshot list** entries include:

* the snapshot's unique ID
* the snapshot's creation time
* the previous snapshot's creation time, if available. Use this time to make sure that, on restore, you pick the correct snapshot, which is guaranteed to have the data you want to bring back.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    list_snapshot_schedules <schedule-id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *schedule-id*: The snapshot schedule's unique identifier. Optional; omit the ID to return all schedules in the system.

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    list_snapshot_schedules 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256
```

```output.json
{
  "schedules": [
    {
      "id": "6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256",
      "options": {
        "interval": "60.000s",
        "retention": "600.000s"
      },
      "snapshots": [
        {
          "id": "386740da-dc17-4e4a-9a2b-976968b1deb5",
          "snapshot_time_utc": "2021-04-28T13:35:32.499002+0000"
        },
        {
          "id": "aaf562ca-036f-4f96-b193-f0baead372e5",
          "snapshot_time_utc": "2021-04-28T13:36:37.501633+0000",
          "previous_snapshot_time_utc": "2021-04-28T13:35:32.499002+0000"
        }
      ]
    }
  ]
}
```

#### edit_snapshot_schedule

Edits a snapshot schedule. A schedule consists of a list of objects to be included in a snapshot, a time interval at which to take snapshots for them, and a retention time.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    edit_snapshot_schedule <schedule-id> \
    [ interval <snapshot-interval> ] \
    [ retention <retention-time> ] \
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *schedule-id*: The identifier (ID) of the schedule to be edited.
* *snapshot-interval*: The frequency at which to take snapshots, in minutes.
* *retention-time*: The number of minutes to keep a snapshot before deleting it.

**Example**

Edit a snapshot schedule to take a snapshot once every 90 minutes, and retain each snapshot for 3 hours:

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    edit_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 \
    interval 90 retention 180
```

#### restore_snapshot_schedule

Schedules group a set of items into a single tracking object (the *schedule*). When you restore, you can choose a particular schedule and a point in time, and revert the state of all affected objects back to the chosen time.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    restore_snapshot_schedule <schedule-id> <restore-target>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *schedule-id*: The identifier (ID) of the schedule to be restored.
* *restore-target*: The time to which to restore the snapshots in the schedule. This can be either an absolute Unix timestamp, or a relative time such as `minus 5m` (to restore to 5 minutes ago).

You can also use a [YSQL timestamp](../../api/ysql/datatypes/type_datetime/) or [YCQL timestamp](../../api/ycql/type_datetime/#timestamp) with the restore command, if you like.

In addition to restoring to a particular timestamp, you can also restore from a relative time, such as "ten minutes ago".

When you specify a relative time, you can specify any or all of *days*, *hours*, *minutes*, and *seconds*. For example:

* `minus 5m` to restore from five minutes ago
* `minus 1h` to restore from one hour ago
* `minus 3d` to restore from three days ago
* `minus 1h 5m` to restore from one hour and five minutes ago

Relative times can be in any of the following formats (again, note that you can specify any or all of days, hours, minutes, and seconds):

* ISO 8601: `3d 4h 5m 6s`
* Abbreviated PostgreSQL: `3 d 4 hrs 5 mins 6 secs`
* Traditional PostgreSQL: `3 days 4 hours 5 minutes 6 seconds`
* SQL standard: `D H:M:S`

**Examples**

Restore from an absolute timestamp:

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    restore_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 1617670679185100
```

Restore from a relative time:

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    restore_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 minus 60s
```

In both cases, the output is similar to the following:

```output.json
{
    "snapshot_id": "6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256",
    "restoration_id": "b1b96d53-f9f9-46c5-b81c-6937301c8eff"
}
```

#### delete_snapshot_schedule

Deletes the snapshot schedule with the given ID, **and all of the snapshots** associated with that schedule.

Returns a JSON object with the `schedule_id` that was just deleted.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    delete_snapshot_schedule <schedule-id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *schedule-id*: The snapshot schedule's unique identifier.

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    delete_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256
```

The output should show the schedule ID we just deleted.

```output.json
{
    "schedule_id": "6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256"
}
```

---

<a name="deployment-topology-commands"></a>

### Multi-zone and multi-region deployment commands

#### modify_placement_info

Modifies the placement information (cloud, region, and zone) for a deployment.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    modify_placement_info <placement-info> <replication-factor> \
    [ <placement-id> ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *placement-info*: Comma-delimited list of placements for *cloud*.*region*.*zone*. Optionally, after each placement block, you can also specify a minimum replica count separated by a colon. This count indicates how many minimum replicas of each tablet we want in that placement block. Its default value is 1. It is not recommended to repeat the same placement multiple times but instead specify the total count after the colon. However, if you specify a placement multiple times, the total count from all mentions is taken.
* *replication-factor*: The number of replicas for each tablet. This value should be greater than or equal to the total of replica counts specified in *placement-info*.
* *placement-id*: The identifier of the primary cluster, which can be any unique string. Optional; if not set, a randomly-generated ID is used.

**Example**

```sh
./bin/yb-admin \
    --master_addresses $MASTER_RPC_ADDRS \
    modify_placement_info  \
    aws.us-west.us-west-2a:2,aws.us-west.us-west-2b:2,aws.us-west.us-west-2c 5
```

This will place a minimum of:

1. 2 replicas in aws.us-west.us-west-2a
2. 2 replicas in aws.us-west.us-west-2b
3. 1 replica in aws.us-west.us-west-2c

You can verify the new placement information by running the following `curl` command:

```sh
curl -s http://<any-master-ip>:7000/cluster-config
```

Use the wildcard `*` to allow placement in any zone in a specific region or any region in a specific cloud. For example:

```sh
./bin/yb-admin \
    --master_addresses $MASTER_RPC_ADDRS \
    modify_placement_info  \
    aws.*.*:5 5`
```

This requests a placement of 5 replicas anywhere in the `aws` cloud. Similarly:

```sh
./bin/yb-admin \
    --master_addresses $MASTER_RPC_ADDRS \
    modify_placement_info  \
    aws.us-east-1.*:3 3`
```

This requests a placement of 3 replicas anywhere in the `us-east-1` region of `aws` cloud.

#### set_preferred_zones

Sets the preferred availability zones (AZs) and regions. Tablet leaders are placed in alive and healthy nodes of AZs in order of preference. When no healthy node is available in the most preferred AZs (preference value 1), then alive and healthy nodes from the next preferred AZs are picked. AZs with no preference are equally eligible to host tablet leaders.

Having all tablet leaders reside in a single region reduces the number of network hops for the database to write transactions, which increases performance and reduces latency.

{{< note title="Note" >}}

* Make sure you've already run [modify_placement_info](#modify-placement-info) command beforehand.

* By default, the transaction status tablet leaders don't respect these preferred zones and are balanced across all nodes. Transactions include a roundtrip from the user to the transaction status tablet serving the transaction - using the leader closest to the user rather than forcing a roundtrip to the preferred zone improves performance.

* Leader blacklisted nodes don't host any leaders irrespective of their preference.

* Cluster configuration stores preferred zones in either affinitized_leaders or multi_affinitized_leaders object.

* Tablespaces don't inherit cluster-level placement information, leader preference, or read replica configurations.

* If the client application uses a smart driver, set the [topology keys](/preview/develop/drivers-orms/smart-drivers/#topology-aware-load-balancing) to target the preferred zones.

{{< /note >}}

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    set_preferred_zones <cloud.region.zone>[:<preference>] \
    [<cloud.region.zone>[:<preference>]]...
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *cloud.region.zone*: Specifies the cloud, region, and zone. Default is `cloud1.datacenter1.rack1`.
* *preference*: Specifies the leader preference for a zone. Values have to be contiguous non-zero integers. Multiple zones can have the same value. Default is 1.

**Example**

Suppose you have a deployment in the following regions: `gcp.us-west1.us-west1-a`, `gcp.us-west1.us-west1-b`, `gcp.asia-northeast1.asia-northeast1-a`, and `gcp.us-east4.us-east4-a`. Looking at the cluster configuration:

```sh
curl -s http://<any-master-ip>:7000/cluster-config
```

The following is a sample configuration:

```output
replication_info {
  live_replicas {
    num_replicas: 5
    placement_blocks {
      cloud_info {
        placement_cloud: "gcp"
        placement_region: "us-west1"
        placement_zone: "us-west1-a"
      }
      min_num_replicas: 1
    }
    placement_blocks {
      cloud_info {
        placement_cloud: "gcp"
        placement_region: "us-west1"
        placement_zone: "us-west1-b"
      }
      min_num_replicas: 1
    }
    placement_blocks {
      cloud_info {
        placement_cloud: "gcp"
        placement_region: "us-east4"
        placement_zone: "us-east4-a"
      }
      min_num_replicas: 2
    }
    placement_blocks {
      cloud_info {
        placement_cloud: "gcp"
        placement_region: "us-asia-northeast1"
        placement_zone: "us-asia-northeast1-a"
      }
      min_num_replicas: 1
    }
  }
}
```

The following command sets the preferred region to `gcp.us-west1` and the fallback to zone `gcp.us-east4.us-east4-a`:

```sh
ssh -i $PEM $ADMIN_USER@$MASTER1 \
   ~/master/bin/yb-admin --master_addresses $MASTER_RPC_ADDRS \
    set_preferred_zones \
    gcp.us-west1.us-west1-a:1 \
    gcp.us-west1.us-west1-b:1 \
    gcp.us-east4.us-east4-a:2
```

Verify by running the following.

```sh
curl -s http://<any-master-ip>:7000/cluster-config
```

Looking again at the cluster configuration you should see `multi_affinitized_leaders` added:

```output
replication_info {
  live_replicas {
    num_replicas: 5
    placement_blocks {
      cloud_info {
        placement_cloud: "gcp"
        placement_region: "us-west1"
        placement_zone: "us-west1-a"
      }
      min_num_replicas: 1
    }
    placement_blocks {
      cloud_info {
        placement_cloud: "gcp"
        placement_region: "us-west1"
        placement_zone: "us-west1-b"
      }
      min_num_replicas: 1
    }
    placement_blocks {
      cloud_info {
        placement_cloud: "gcp"
        placement_region: "us-east4"
        placement_zone: "us-east4-a"
      }
      min_num_replicas: 2
    }
    placement_blocks {
      cloud_info {
        placement_cloud: "gcp"
        placement_region: "us-asia-northeast1"
        placement_zone: "us-asia-northeast1-a"
      }
      min_num_replicas: 1
    }
  }
  multi_affinitized_leaders {
    zones {
      placement_cloud: "gcp"
      placement_region: "us-west1"
      placement_zone: "us-west1-a"
    }
    zones {
      placement_cloud: "gcp"
      placement_region: "us-west1"
      placement_zone: "us-west1-b"
    }
  }
  multi_affinitized_leaders {
    zones {
      placement_cloud: "gcp"
      placement_region: "us-east4"
      placement_zone: "us-east4-a"
    }
  }
}
```

### Read replica deployment commands

#### add_read_replica_placement_info

Add a read replica cluster to the master configuration.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    add_read_replica_placement_info <placement-info> \
    <replication-factor> \
    [ <placement-id> ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *placement-info*: A comma-delimited list of read replica placements for *cloud*.*region*.*zone*, using the format `<cloud1.region1.zone1>:<num_replicas_in_zone1>,<cloud2.region2.zone2>:<num_replicas_in_zone2>,...`. Default is `cloud1.datacenter1.rack1`.
    Read replica availability zones must be uniquely different from the primary availability zones. To use the same cloud, region, and availability zone for a read replica as a primary cluster, you can suffix the zone with `_rr` (for read replica). For example, `c1.r1.z1` vs `c1.r1.z1_rr:1`.
* *replication-factor*: The total number of read replicas.
* *placement-id*: The identifier of the read replica cluster, which can be any unique string. If not set, a randomly-generated ID will be used. Primary and read replica clusters must use different placement IDs.

#### modify_read_replica_placement_info

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    modify_read_replica_placement_info <placement-info> \
    <replication-factor> \
    [ <placement-id> ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *placement-info*: A comma-delimited list of placements for *cloud*.*region*.*zone*. Default is `cloud1.datacenter1.rack1`.
* *replication-factor*: The number of replicas.
* *placement-id*: The identifier of the read replica cluster.

#### delete_read_replica_placement_info

Delete the read replica.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    delete_read_replica_placement_info
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.

---

### Security commands

#### Encryption at rest commands

For details on using encryption at rest, see [Encryption at rest](../../secure/encryption-at-rest).

#### add_universe_key_to_all_masters

Sets the contents of *key-path* in-memory on each YB-Master node.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    add_universe_key_to_all_masters <key-id> <key-path>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *key-id*: Universe-unique identifier (can be any string, such as a string of a UUID) that will be associated to the universe key contained in the contents of *key-path* as a byte[].
* *key-path*:  The path to the file containing the universe key.

{{< note title="Note" >}}

After adding the universe keys to all YB-Master nodes, you can verify the keys exist using the [all_masters_have_universe_key_in_memory](#all-masters-have-universe-key-in-memory) command and enable encryption using the [rotate_universe_key_in_memory](#rotate-universe-key-in-memory) command.

{{< /note >}}

#### all_masters_have_universe_key_in_memory

Checks whether the universe key associated with the provided *key-id* exists in-memory on each YB-Master node.

```sh
yb-admin \
    --master_addresses <master-addresses> all_masters_have_universe_key_in_memory <key-id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *key-id*: Universe-unique identifier (can be any string, such as a string of a UUID) that will be associated to the universe key contained in the contents of *key-path* as a byte[].

#### rotate_universe_key_in_memory

Rotates the in-memory universe key to start encrypting newly-written data files with the universe key associated with the provided *key-id*.

{{< note title="Note" >}}

The [all_masters_have_universe_key_in_memory](#all-masters-have-universe-key-in-memory) value must be true for the universe key to be successfully rotated and enabled).

{{< /note >}}

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> rotate_universe_key_in_memory <key-id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *key-id*: Universe-unique identifier (can be any string, such as a string of a UUID) that will be associated to the universe key contained in the contents of *key-path* as a byte[].

#### disable_encryption_in_memory

Disables the in-memory encryption at rest for newly-written data files.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    disable_encryption_in_memory
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.

#### is_encryption_enabled

Checks if cluster-wide encryption is enabled.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    is_encryption_enabled
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.

Returns message:

```output
Encryption status: ENABLED with key id <key_id_2>
```

The new key ID (`<key_id_2>`) should be different from the previous one (`<key_id>`).

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    is_encryption_enabled
```

```output
Encryption status: ENABLED with key id <key_id_2>
```

### Change Data Capture (CDC) commands

#### create_change_data_stream

Create a change data capture (CDC) DB stream for the specified namespace using the following command.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    create_change_data_stream ysql.<namespace-name>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *namespace-name*: The namespace on which the DB stream ID is to be created.

For example:

```sh
./bin/yb-admin \
    --master_addresses 127.0.0.1:7100 \
    create_change_data_stream ysql.yugabyte
```

##### Creating a stream for Transactional CDC

Create a change data capture (CDC) DB stream for the specified namespace that can be used for Transactional CDC using the following command.
This feature is {{<tags/feature/tp>}}. Use the [yb_enable_cdc_consistent_snapshot_streams](../../reference/configuration/yb-tserver/#yb-enable-cdc-consistent-snapshot-streams) flag to enable the feature.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    create_change_data_stream ysql.<namespace-name> [EXPLICIT] [<before-image-mode>] [USE_SNAPSHOT | NOEXPORT_SNAPSHOT]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *namespace-name*: The namespace on which the DB stream ID is to be created.
* EXPLICIT: Checkpointing type on the server. See [Creating stream in EXPLICIT checkpointing mode](#creating-stream-in-explicit-checkpointing-mode).
* *before-image-mode*: Record type indicating to the server that the stream should send only the new values of the changed columns. See [Enabling before image](#enabling-before-image).
* USE_SNAPSHOT: Snapshot option indicating intention of client to consume the snapshot. If you don't want the client to consume the snapshot, use the NOEXPORT_SNAPSHOT option.

For example:

```sh
./bin/yb-admin \
    --master_addresses 127.0.0.1:7100 \
    create_change_data_stream ysql.yugabyte EXPLICIT CHANGE USE_SNAPSHOT
```

##### Enabling before image

To create a change data capture (CDC) DB stream which also supports sending the before image of the record, use the following command.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    create_change_data_stream ysql.<namespace-name> [EXPLICIT] <before-image-mode>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *namespace-name*: The namespace on which the DB stream ID is to be created.
* EXPLICIT: Checkpointing type on the server. See [Creating stream in EXPLICIT checkpointing mode](#creating-stream-in-explicit-checkpointing-mode).
* *before-image-mode*: Record type indicating to the server that the stream should send only the new values of the changed columns. Refer to [Before image modes](../../additional-features/change-data-capture/using-yugabytedb-grpc-replication/cdc-get-started/#before-image-modes).

A successful operation of the above command returns a message with a DB stream ID:

```output
CDC Stream ID: d540f5e4890c4d3b812933cbfd703ed3
```

##### Creating stream in EXPLICIT checkpointing mode

To create a change data capture (CDC) DB stream which works in the EXPLICIT checkpointing mode where the client is responsible for managing the checkpoints, use the following command:

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    create_change_data_stream ysql.<namespace-name> EXPLICIT
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *namespace-name*: The namespace on which the DB stream ID is to be created.
* EXPLICIT: Checkpointing type on the server.

A successful operation of the above command returns a message with a DB stream ID:

```output
CDC Stream ID: d540f5e4890c4d3b812933cbfd703ed3
```

{{< note title="IMPLICIT checkpointing is deprecated" >}}

It is recommended that you create streams in EXPLICIT checkpointing mode only (the default). IMPLICIT checkpointing mode will be completely removed in future releases.

{{< /note >}}

#### list_change_data_streams

Lists all the created CDC DB streams.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    list_change_data_streams [namespace-name]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *namespace-name*: (Optional) The namespace name for which to list the streams. If not specified, all streams are listed without filtering.

**Example:**

```sh
./bin/yb-admin \
    --master_addresses 127.0.0.1:7100 \
    list_change_data_streams
```

This command results in the following response. It will have all the table IDs associated with the stream ID:

```output
CDC Streams:
streams {
  stream_id: "d540f5e4890c4d3b812933cbfd703ed3"
  table_id: "000033e1000030008000000000004000"
  options {
    key: "id_type"
    value: "NAMESPACEID"
  }
  options {
    key: "checkpoint_type"
    value: "EXPLICIT"
  }
  options {
    key: "source_type"
    value: "CDCSDK"
  }
  options {
    key: "record_format"
    value: "PROTO"
  }
  options {
    key: "record_type"
    value: "CHANGE"
  }
  options {
    key: "state"
    value: "ACTIVE"
  }
}
```

#### get_change_data_stream_info

Get the information associated with a particular CDC DB stream.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    get_change_data_stream_info <db-stream-id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *db-stream-id*: The CDC DB stream ID to get the info of.

**Example:**

```sh
./bin/yb-admin \
    --master_addresses 127.0.0.1:7100 \
    get_change_data_stream_info d540f5e4890c4d3b812933cbfd703ed3
```

The previous command results in the following response. It will have the table_id(s) associated with the stream and the namespace_id on which the stream is created:

```output
CDC DB Stream Info:
table_info {
  stream_id: "d540f5e4890c4d3b812933cbfd703ed3"
  table_id: "000033e1000030008000000000004000"
}
namespace_id: "000033e1000030008000000000000000"
```

#### delete_change_data_stream

Delete the specified CDC DB stream.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    delete_change_data_stream <db-stream-id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *db-stream-id*: The CDC DB stream ID to be deleted.

**Example:**

```sh
./bin/yb-admin \
    --master_addresses 127.0.0.1:7100 \
    delete_change_data_stream d540f5e4890c4d3b812933cbfd703ed3
```

The above command results in the following response:

```output
Successfully deleted CDC DB Stream ID: d540f5e4890c4d3b812933cbfd703ed3
```

### xCluster Replication Commands

For detailed step-by-step instructions on deploying xCluster, refer to the [Deploy xCluster](../../deploy/multi-dc/async-replication). For monitoring xCluster, refer to [Monitor xCluster](../../launch-and-manage/monitor-and-alert/xcluster-monitor).

#### setup_universe_replication

Sets up the universe replication for the specified source universe. Use this command only if no tables have been configured for replication. If tables are already configured for replication, use [alter_universe_replication](#alter-universe-replication) to add more tables.

To verify if any tables are already configured for replication, use [list_cdc_streams](#list-cdc-streams).

**Syntax**

```sh
yb-admin \
    --master_addresses <target-master-addresses> \
    setup_universe_replication \
    <replication-group-id> \
    <source-master-addresses> \
    <source-table-ids> \
    [ <bootstrap-ids> ] \
    [ transactional ]
```

* *target-master-addresses*: Comma-separated list of target YB-Master hosts and ports. Default is `localhost:7100`.
* *replication-group-id*: The replication group identifier.
* *source-master-addresses*: Comma-separated list of the source master addresses.
* *source-table-ids*: Comma-separated list of source universe table identifiers (`table_id`).
* *bootstrap-ids*: Comma-separated list of source universe bootstrap identifiers (`bootstrap_id`). Obtain these with [bootstrap_cdc_producer](#bootstrap-cdc-producer-comma-separated-list-of-table-ids), using a comma-separated list of source universe table IDs.
* `transactional`: identifies the universe as Active in a transactional xCluster deployment.

{{< warning title="Important" >}}
Enter the source universe bootstrap IDs in the same order as their corresponding table IDs.
{{< /warning >}}

{{< note title="Tip" >}}

To display a list of tables and their UUID (`table_id`) values, open the **YB-Master UI** (`<master_host>:7000/`) and click **Tables** in the navigation bar.

{{< /note >}}

**Example**

```sh
./bin/yb-admin \
    --master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
    setup_universe_replication e260b8b6-e89f-4505-bb8e-b31f74aa29f3_xClusterSetup1 \
    127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
    000030a5000030008000000000004000,000030a5000030008000000000004005,dfef757c415c4b2cacc9315b8acb539a
```

#### alter_universe_replication

Changes the universe replication for the specified source universe. Use this command to do the following:

* Add or remove tables in an existing replication UUID.
* Modify the source master addresses.

If no tables have been configured for replication, use [setup_universe_replication](#setup-universe-replication).

To check if any tables are configured for replication, use [list_cdc_streams](#list-cdc-streams).

**Syntax**

Use the `set_master_addresses` subcommand to replace the source master address list. Use this if the set of masters on the source changes:

```sh
yb-admin --master_addresses <target-master-addresses> \
    alter_universe_replication <replication-group-id> \
    set_master_addresses <source-master-addresses>
```

* *target-master-addresses*: Comma-separated list of target YB-Master hosts and ports. Default is `localhost:7100`.
* *replication-group-id*: The replication group identifier.
* *source-master-addresses*: Comma-separated list of the source master addresses.

Use the `add_table` subcommand to add one or more tables to the existing list:

```sh
yb-admin --master_addresses <target-master-addresses> \
    alter_universe_replication <replication-group-id> \
    add_table <source-table-ids> \
    [ <bootstrap-ids> ]
```

* *target-master-addresses*: Comma-separated list of target YB-Master hosts and ports. Default is `localhost:7100`.
* *replication-group-id*: The replication group identifier.
* *source-table-ids*: Comma-separated list of source universe table identifiers (`table_id`).
* *bootstrap-ids*: Comma-separated list of source universe bootstrap identifiers (`bootstrap_id`). Obtain these with [bootstrap_cdc_producer](#bootstrap-cdc-producer-comma-separated-list-of-table-ids), using a comma-separated list of source universe table IDs.

{{< warning title="Important" >}}
Enter the source universe bootstrap IDs in the same order as their corresponding table IDs.
{{< /warning >}}

Use the `remove_table` subcommand to remove one or more tables from the existing list:

```sh
yb-admin --master_addresses <target-master-addresses> \
    alter_universe_replication <replication-group-id> \
    remove_table <source-table-ids> [ignore-errors]
```

* *target-master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *replication-group-id*: The replication group identifier.
* *source-table-ids*: Comma-separated list of source universe table identifiers (`table_id`).
* `ignore-errors`: Execute the command, ignoring any errors. It is recommended that you contact support before using this option.

Use the `rename_id` subcommand to rename xCluster replication streams.

```sh
yb-admin --master_addresses <target-master-addresses> \
    alter_universe_replication <replication-group-id> \
    rename_id <new-replication-group-id>
```

* *target-master-addresses*: Comma-separated list of target YB-Master hosts and ports. Default is `localhost:7100`.
* *replication-group-id*: The existing replication group identifier.
* *new-replication-group-id*: The new replication group identifier.

#### delete_universe_replication <source_universe_uuid>

Deletes universe replication for the specified source universe.

**Syntax**

```sh
yb-admin \
    --master_addresses <target-master-addresses> \
    delete_universe_replication <replication-group-id>
```

* *target-master-addresses*: Comma-separated list of target YB-Master hosts and ports. Default is `localhost:7100`.
* *replication-group-id*: The replication group identifier.

#### set_universe_replication_enabled

Sets the universe replication to be enabled or disabled.

**Syntax**

```sh
yb-admin \
    --master_addresses <target-master-addresses> \
    set_universe_replication_enabled <replication-group-id> [0|1]
```

* *target-master-addresses*: Comma-separated list of target YB-Master hosts and ports. Default is `localhost:7100`.
* *replication-group-id*: The replication group identifier.
* `0` | `1`: Disabled (`0`) or enabled (`1`). Default is `1`.

#### get_xcluster_safe_time

Reports the current xCluster safe time for each namespace, which is the time at which reads will be performed.

**Syntax**

```sh
yb-admin \
    --master_addresses <target-master-addresses> \
    get_xcluster_safe_time \
    [include_lag_and_skew]
```

* *target-master-addresses*: Comma-separated list of target YB-Master hosts and ports. Default is `localhost:7100`.
* `include_lag_and_skew`: Display the `safe_time_lag_sec` and `safe_time_skew_sec`.

**Example**

```sh
./bin/yb-admin \
    --master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
    get_xcluster_safe_time include_lag_and_skew
```

```output
{
    "namespace_id": "000033f1000030008000000000000000",
    "namespace_name": "yugabyte",
    "safe_time": "2023-04-14 18:34:18.429430",
    "safe_time_epoch": "1681522458429430",
    "safe_time_lag_sec": "15.66",
    "safe_time_skew_sec": "14.95"
}
```

* *namespace_id*: ID of the stream.
* *namespace_name*: Name of the stream.
* *safe_time*: Safe time in timestamp format.
* *safe_time_epoch*: The `epoch` of the safe time.
* *safe_time_lag_sec*: Safe time lag is computed as `(current time - current safe time)`.
* *safe_time_skew_sec*: Safe time skew is computed as `(safe time of most caught up tablet - safe time of laggiest tablet)`.

#### wait_for_replication_drain

Verify when the producer and consumer are in sync for a given list of `stream_ids` at a given timestamp.

**Syntax**

```sh
yb-admin \
    --master_addresses <source-master-addresses> \
    wait_for_replication_drain \
    <stream-ids> [<timestamp> | minus <interval>]
```

* *source-master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *stream-ids*: Comma-separated list of stream IDs.
* *timestamp*: The time to which to wait for replication to drain. If not provided, it will be set to current time in the YB-Master API.
* `minus <interval>`: The same format as described in [Restore from a relative time](../../explore/cluster-management/point-in-time-recovery-ysql/#restore-from-a-relative-time), or see [restore_snapshot_schedule](#restore-snapshot-schedule).

**Example**

```sh
./bin/yb-admin \
    --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
    wait_for_replication_drain 000033f1000030008000000000000000,200033f1000030008000000000000002 minus 1m
```

If all streams are caught-up, the API outputs `All replications are caught-up.` to the console.

Otherwise, it outputs the non-caught-up streams in the following format:

```output
Found undrained replications:
- Under Stream <stream_id>:
  - Tablet: <tablet_id>
  - Tablet: <tablet_id>
  // ......
// ......
```

#### list_cdc_streams

Lists the xCluster outbound streams.

{{< note title="Tip" >}}

Use this command when setting up xCluster replication to verify if any tables are configured for replication. If not, run [setup_universe_replication](#setup-universe-replication); if tables are already configured for replication, use [alter_universe_replication](#alter-universe-replication) to add more tables.

{{< /note >}}

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    list_cdc_streams
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.

**Example**

```sh
./bin/yb-admin \
    --master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
    list_cdc_streams
```

#### delete_cdc_stream

Deletes underlying xCluster outbound streams.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    delete_cdc_stream <stream-id> \
    [force_delete]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *stream-id*: The ID of the xCluster stream.
* `force_delete`: Force the delete operation.

{{< note title="Note" >}}
This command should only be needed for advanced operations, such as doing manual cleanup of old bootstrapped streams that were never fully initialized, or otherwise failed replication streams. For normal xCluster replication cleanup, use [delete_universe_replication](#delete-universe-replication-source-universe-uuid).
{{< /note >}}

#### bootstrap_cdc_producer

Mark a set of tables in preparation for setting up xCluster replication.

**Syntax**

```sh
yb-admin \
    --master_addresses <source-master-addresses> \
    bootstrap_cdc_producer <source-table-ids>
```

* *source-master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *source-table-ids*: Comma-separated list of unique UUIDs associated with the tables (`table_id`).

**Example**

```sh
./bin/yb-admin \
    --master_addresses 172.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
    bootstrap_cdc_producer 000030ad000030008000000000004000
```

```output
table id: 000030ad000030008000000000004000, CDC bootstrap id: dd5ea73b5d384b2c9ebd6c7b6d05972c
```

{{< note title="Note" >}}
The xCluster bootstrap IDs are the ones that should be used with [setup_universe_replication](#setup-universe-replication) and [alter_universe_replication](#alter-universe-replication).
{{< /note >}}

#### get_replication_status

Returns the xCluster replication status of all inbound replication groups. If *replication-group-id* is provided, this will only return streams that belong to an associated replication group.

**Syntax**

```sh
yb-admin \
    --master_addresses <target-master-addresses> \
    get_replication_status [ <replication-group-id> ]
```

* *target-master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *replication-group-id*: The replication group identifier.

**Example**

```sh
./bin/yb-admin \
    --master_addresses 172.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
    get_replication_status e260b8b6-e89f-4505-bb8e-b31f74aa29f3
```

```output
statuses {
  table_id: "03ee1455f2134d5b914dd499ccad4377"
  stream_id: "53441ad2dd9f4e44a76dccab74d0a2ac"
  errors {
    error: REPLICATION_MISSING_OP_ID
    error_detail: "Unable to find expected op id on the producer"
  }
}
```

#### create_xcluster_checkpoint

Checkpoint namespaces for use in xCluster replication.

**Syntax**

```sh
yb-admin \
    --master_addresses <source-master-addresses> \
    create_xcluster_checkpoint \
    <replication-group-id> \
    <namespace_names> \
    [automatic_ddl_mode]
```

* *source-master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *replication-group-id*: The replication group identifier.
* *namespace_names*: Comma-separated list of namespaces.
* `automatic_ddl_mode`: Use Automatic xCluster mode. {{<tags/feature/ea idea="2176">}}

#### is_xcluster_bootstrap_required

Checks if the databases of a previously checkpointed replication group requires a bootstrap (backup/restore) of the database to the target universe.

**Syntax**

```sh
yb-admin \
    --master_addresses <source-master-addresses> \
    is_xcluster_bootstrap_required \
    <replication-group-id> \
    <namespace-names>
```

* *source-master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *replication-group-id*: The replication group identifier.
* *namespace-names*: Comma-separated list of namespaces.

#### setup_xcluster_replication

Setup xCluster replication using a previously created [checkpoint](#create-xcluster-checkpoint).

**Syntax**

```sh
yb-admin \
    --master_addresses <source-master-addresses> \
    setup_xcluster_replication \
    <replication-group-id> \
    <target-master-addresses>
```

* *source-master-addresses*: Comma-separated list of source universe YB-Master hosts and ports. Default is `localhost:7100`.
* *replication-group-id*: The replication group identifier.
* *target-master-addresses*: Comma-separated list of target universe YB-Master hosts and ports. Default is `localhost:7100`.

#### drop_xcluster_replication

Drops the xCluster replication group. If *target-master-addresses* are provided, it will also drop the replication on the target universe.

**Syntax**

```sh
yb-admin \
    --master_addresses <source-master-addresses> \
    drop_xcluster_replication \
    <replication-group-id> \
    [<target-master-addresses>]
```

* *source-master-addresses*: Comma-separated list of source universe YB-Master hosts and ports. Default is `localhost:7100`.
* *replication-group-id*: The replication group identifier.
* *target-master-addresses*: Comma-separated list of target universe YB-Master hosts and ports. Default is `localhost:7100`.

#### add_namespace_to_xcluster_checkpoint

Adds a database to an existing xCluster checkpoint.

**Syntax**

```sh
yb-admin \
    --master_addresses <source-master-addresses> \
    add_namespace_to_xcluster_checkpoint \
    <replication-group-id> \
    <namespace-name>
```

* *source-master-addresses*: Comma-separated list of source universe YB-Master hosts and ports. Default is `localhost:7100`.
* *replication-group-id*: The replication group identifier.
* *namespace-name*: The namespace to checkpoint.

#### add_namespace_to_xcluster_replication

Adds a database to an existing xCluster replication after it has been checkpointed (and bootstrapped if needed).

**Syntax**

```sh
yb-admin \
    --master_addresses <source-master-addresses> \
    add_namespace_to_xcluster_replication \
    <replication-group-id> \
    <namespace-name> \
    <target-master-addresses>
```

* *source-master-addresses*: Comma-separated list of source universe YB-Master hosts and ports. Default is `localhost:7100`.
* *replication-group-id*: The replication group identifier.
* *namespace-name*: The namespace name.
* *target-master-addresses*: Comma-separated list of target universe YB-Master hosts and ports. Default is `localhost:7100`.

#### remove_namespace_from_xcluster_replication

Removes a database from an existing xCluster replication. If target master addresses are provided, it will also remove the database from the target universe xCluster metadata.

**Syntax**

```sh
yb-admin \
    --master_addresses <source-master-addresses> \
    remove_namespace_from_xcluster_replication \
    <replication-group-id> \
    <namespace-name> \
    [<target-master-addresses>]
```

* *source-master-addresses*: Comma-separated list of source universe YB-Master hosts and ports. Default is `localhost:7100`.
* *replication-group-id*: The replication group identifier.
* *namespace-name*: The namespace name.
* *target-master-addresses*: Comma-separated list of target universe YB-Master hosts and ports. Default is `localhost:7100`.

#### list_xcluster_outbound_replication_groups

List The replication group identifiers for all outbound xCluster replications. If namespace-id is provided, only the replication groups for that namespace will be returned.

**Syntax**

```sh
yb-admin \
    --master_addresses <source-master-addresses> \
    list_xcluster_outbound_replication_groups \
    [<namespace-id>]
```

* *source-master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *namespace-id*: The namespace UUID.

#### get_xcluster_outbound_replication_group_info

Display the status of a specific outbound xCluster replication group.

**Syntax**

```sh
yb-admin \
    --master_addresses <source-master-addresses> \
    get_xcluster_outbound_replication_group_info \
    <replication-group-id>
```

* *source-master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *replication-group-id*: The replication group identifier.

#### list_universe_replications

List The replication group identifiers for all inbound xCluster replications. If *namespace-id* is provided, only the replication groups for that namespace will be returned.

**Syntax**

```sh
yb-admin \
    --master_addresses <target-master-addresses> \
    list_universe_replications \
    [<namespace-id>]
```

* *target-master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *namespace-id*: The namespace UUID.

---

### Decommissioning commands

#### get_leader_blacklist_completion

Gets the tablet load move completion percentage for blacklisted nodes.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    get_leader_blacklist_completion
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    get_leader_blacklist_completion
```

#### change_blacklist

Changes the blacklist for YB-TServer servers.

After old YB-TServer servers are terminated, you can use this command to clean up the blacklist.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    change_blacklist [ ADD | REMOVE ] <ip_addr>:<port> \
    [ <ip_addr>:<port> ]...
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* ADD | REMOVE: Adds or removes the specified YB-TServer server from blacklist.
* *ip_addr:port*: The IP address and port of the YB-TServer.

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    change_blacklist \
      ADD node1:9100 node2:9100 node3:9100 node4:9100 node5:9100 node6:9100
```

#### change_leader_blacklist

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    change_leader_blacklist [ ADD | REMOVE ] <ip_addr>:<port> \
    [ <ip_addr>:<port> ]...
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* ADD | REMOVE: Adds or removes the specified YB-Master, or YB-TServer from leader blacklist.
* *ip_addr:port*: The IP address and port of the YB-TServer.

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    change_leader_blacklist \
      ADD node1:9100 node2:9100 node3:9100 node4:9100 node5:9100 node6:9100
```

#### leader_stepdown

Forces the YB-TServer leader of the specified tablet to step down.

{{< note title="Note" >}}

Use this command only if recommended by Yugabyte support.

There is a possibility of downtime.

{{< /note >}}

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    leader_stepdown <tablet-id> <dest-ts-uuid>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *tablet-id*: The identifier (ID) of the tablet.
* *dest-ts-uuid*: The destination identifier (UUID) for the new YB-TServer leader. To move leadership **from** the current leader, when you do not need to specify a new leader, use `""` for the value. If you want to transfer leadership intentionally **to** a specific new leader, then specify the new leader.

{{< note title="Note" >}}

If specified, *dest-ts-uuid* becomes the new leader. If the argument is empty (`""`), then a new leader will be elected automatically. In a future release, this argument will be optional. See GitHub issue [#4722](https://github.com/yugabyte/yugabyte-db/issues/4722)

{{< /note >}}

---

### Rebalancing commands

For information on YB-Master load balancing, see [Data placement and load balancing](../../architecture/yb-master/#tablet-assignments).

For YB-Master load balancing flags, see [Load balancing flags](../../reference/configuration/yb-master/#load-balancing-flags).

#### set_load_balancer_enabled

Enables or disables the load balancer.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    set_load_balancer_enabled [ 0 | 1 ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* `0` | `1`: Enabled (`1`) is the default. To disable, set to `0`.

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    set_load_balancer_enabled 0
```

#### get_load_balancer_state

Returns the cluster load balancer state.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> get_load_balancer_state
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.

#### get_load_move_completion

Checks the percentage completion of the data move.

You can rerun this command periodically until the value reaches `100.0`, indicating that the data move has completed.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    get_load_move_completion
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.

{{< note title="Note" >}}

The time needed to complete a data move depends on the following:

* number of tablets and tables
* size of each of those tablets
* SSD transfer speeds
* network bandwidth between new nodes and existing ones

{{< /note >}}

For an example of performing a data move and the use of this command, refer to [Change cluster configuration](../../manage/change-cluster-config/).

**Example**

In the following example, the data move is `66.6` percent done.

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    get_load_move_completion
```

Returns the following percentage:

```output
66.6
```

#### get_is_load_balancer_idle

Finds out if the load balancer is idle.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    get_is_load_balancer_idle
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    get_is_load_balancer_idle
```

---

### Upgrade

Refer to [Upgrade a deployment](../../manage/upgrade-deployment/) to learn about how to upgrade a YugabyteDB cluster.

For information on AutoFlags and how it secures upgrades with new data formats, refer to [AutoFlags](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/auto_flags.md).

#### get_auto_flags_config

Returns the current AutoFlags configuration of the universe.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    get_auto_flags_config
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.

**Example**

```sh
./bin/yb-admin --master_addresses ip1:7100,ip2:7100,ip3:7100 get_auto_flags_config
```

If the operation is successful you should see output similar to the following:

```output
AutoFlags config:
config_version: 1
promoted_flags {
  process_name: "yb-master"
  flags: "enable_automatic_tablet_splitting"
  flags: "master_enable_universe_uuid_heartbeat_check"
  flag_infos {
    promoted_version: 1
  }
  flag_infos {
    promoted_version: 1
  }
}
promoted_flags {
  process_name: "yb-tserver"
  flags: "regular_tablets_data_block_key_value_encoding"
  flags: "remote_bootstrap_from_leader_only"
  flags: "ysql_yb_enable_expression_pushdown"
  flag_infos {
    promoted_version: 1
  }
  flag_infos {
    promoted_version: 1
  }
  flag_infos {
    promoted_version: 1
  }
}
```

#### promote_auto_flags

After all YugabyteDB processes have been upgraded to the new version, these features can be enabled by promoting their AutoFlags.

Note that `promote_auto_flags` is a cluster-level operation; you don't need to run it on every node.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    promote_auto_flags \
    [<max-flags-class> [<promote-non-runtime-flags> [force]]]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.
* *max-flags-class*: The maximum AutoFlag class to promote. Allowed values are `kLocalVolatile`, `kLocalPersisted` and `kExternal`. Default is `kExternal`.
* *promote-non-runtime-flags*: Weather to promote non-runtime flags. Allowed values are `true` and `false`. Default is `true`.
* `force`: Forces the generation of a new AutoFlag configuration and sends it to all YugabyteDB processes even if there are no new AutoFlags to promote.

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    promote_auto_flags kLocalPersisted
```

If the operation is successful you should see output similar to the following:

```output
PromoteAutoFlags status:
New AutoFlags were promoted. Config version: 2
```

OR

```output
PromoteAutoFlags status:
No new AutoFlags to promote
```

#### upgrade_ysql

Upgrades the YSQL system catalog after a successful [YugabyteDB cluster upgrade](../../manage/upgrade-deployment/).

YSQL upgrades are not required for clusters where [YSQL is not enabled](../../reference/configuration/yb-tserver/#ysql).

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    upgrade_ysql
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.

**Example**

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    upgrade_ysql
```

A successful upgrade returns the following message:

```output
YSQL successfully upgraded to the latest version
```

In certain scenarios, a YSQL upgrade can take longer than 60 seconds, which is the default timeout value for yb-admin. To account for that, run the command with a higher timeout value:

```sh
./bin/yb-admin \
    --master_addresses ip1:7100,ip2:7100,ip3:7100 \
    --timeout_ms 180000 \
    upgrade_ysql
```

Running `upgrade_ysql` is an online operation and doesn't require stopping a running cluster. `upgrade_ysql` is also a cluster-level operation; you don't need to run it on every node.

{{< note title="Note" >}}
Concurrent operations in a cluster can lead to various transactional conflicts, catalog version mismatches, and read restart errors. This is expected, and should be addressed by rerunning the upgrade command.
{{< /note >}}

#### finalize_upgrade

Finalizes an upgrade after a successful [YSQL major upgrade](../../manage/ysql-major-upgrade-local/). You can run this command from any node in the cluster.

Note that `finalize_upgrade` is a cluster-level operation; you don't need to run it on every node.

**Syntax**

```sh
yb-admin \
    --master_addresses <master-addresses> \
    finalize_upgrade
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default is `localhost:7100`.

**Example**

```sh
./bin/yb-admin --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 finalize_upgrade
```

```output
Finalizing YSQL major catalog upgrade
Finalize successful

Promoting auto flags
PromoteAutoFlags completed successfully
New AutoFlags were promoted
New config version: 2

Upgrading YSQL
YSQL successfully upgraded to the latest version

Upgrade successfully finalized
```

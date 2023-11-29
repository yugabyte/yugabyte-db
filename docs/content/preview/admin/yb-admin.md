---
title: yb-admin - command line tool for advanced YugabyteDB administration
headerTitle: yb-admin
linkTitle: yb-admin
description: Use the yb-admin command line tool for advanced administration of YugabyteDB clusters.
menu:
  preview:
    identifier: yb-admin
    parent: admin
    weight: 30
type: docs
---

The `yb-admin` utility, located in the `bin` directory of YugabyteDB home, provides a command line interface for administering clusters.

It invokes the [`yb-master`](../../reference/configuration/yb-master/) and [`yb-tserver`](../../reference/configuration/yb-tserver/) servers to perform the necessary administration.

## Syntax

To use the `yb-admin` utility from the YugabyteDB home directory, run `./bin/yb-admin` using the following syntax.

```sh
yb-admin \
    [ -master_addresses <master-addresses> ]  \
    [ -init_master_addrs <master-address> ]  \
    [ -timeout_ms <millisec> ] \
    [ -certs_dir_name <dir_name> ] \
    <command> [ command_flags ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *init_master_addrs*: Allows specifying a single YB-Master address from which the rest of the YB-Masters are discovered.
* *timeout_ms*: The RPC timeout, in milliseconds. Default value is `60000`. A value of `0` means don't wait; `-1` means wait indefinitely.
* *certs_dir_name*: The directory with certificates to use for secure server connections. Default value is `""`.

  To connect to a cluster with TLS enabled, you must include the `-certs_dir_name` flag with the directory location where the root certificate is located.
* *command*: The operation to be performed. See command for syntax details and examples.
* *command_flags*: Configuration flags that can be applied to the command.

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
    -master_addresses <master-addresses> \
    get_universe_config
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

#### change_config

Changes the configuration of a tablet.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    change_config <tablet_id> \
    [ ADD_SERVER | REMOVE_SERVER ] \
    <peer_uuid> \
    [ PRE_VOTER | PRE_OBSERVER ]
```

* *master_addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *tablet_id*: The identifier (ID) of the tablet.
* ADD_SERVER | REMOVE_SERVER: Subcommand to add or remove the server.
* *peer_uuid*: The UUID of the tablet server hosting the peer tablet.
* *PRE_VOTER | PRE_OBSERVER*: Role of the new peer joining the quorum. Required when using the `ADD_SERVER` subcommand.

**Notes:**

If you need to take a node down temporarily, but intend to bring it back up, you should not need to use the `REMOVE_SERVER` subcommand.

* If the node is down for less than 15 minutes, it will catch up through RPC calls when it comes back online.
* If the node is offline longer than 15 minutes, then it will go through Remote Bootstrap, where the current leader will forward all relevant files to catch up.

If you do not intend to bring a node back up (perhaps you brought it down for maintenance, but discovered that the disk is bad), then you want to decommission the node (using the `REMOVE_SERVER` subcommand) and then add in a new node (using the `ADD_SERVER` subcommand).

#### change_master_config

Changes the master configuration.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    change_master_config \
    [ ADD_SERVER|REMOVE_SERVER ] \
    <ip_addr> <port> \
    [<uuid>]
```

* *master_addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* ADD_SERVER | REMOVE_SERVER: Adds or removes a new YB-Master server.
  * After adding or removing a node, verify the status of the YB-Master server on the YB-Master UI page (<http://node-ip:7000>) or run the [`yb-admin dump_masters_state` command](#dump-masters-state).
* *ip_addr*: The IP address of the server node.
* *port*: The port of the server node.
* *uuid*: The UUID for the server that is being added/removed.

#### list_tablet_servers

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_tablet_servers <tablet_id>
```

* *master_addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *tablet_id*: The identifier (ID) of the tablet.

#### list_tablets

Lists all tablets and their replica locations for a particular table.

Use this to find out who the LEADER of a tablet is.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_tablets <keyspace_type>.<keyspace_name> <table> [max_tablets]
```

* *master_addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *keyspace_type*: Type of the keyspace, ysql or ycql.
* *keyspace_name*: The namespace, or name of the database or keyspace.
* *table*: The name of the table.
* *max_tablets*: The maximum number of tables to be returned. Default is `10`. Set to `0` to return all tablets.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
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
    -master_addresses <master-addresses> \
    list_all_tablet_servers
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

#### list_all_masters

Displays a list of all YB-Master servers in a table listing the master UUID, RPC host and port, state (`ALIVE` or `DEAD`), and role (`LEADER`, `FOLLOWER`, or `UNKNOWN_ROLE`).

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_all_masters
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

**Example**

```sh
./bin/yb-admin \
    -master_addresses node7:7100,node8:7100,node9:7100 \
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
    -master_addresses <master-addresses> \
    list_replica_type_counts <keyspace> <table_name>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *keyspace*: The name of the database or keyspace.
* *table_name*: The name of the table.

#### dump_masters_state

Prints the status of the YB-Master servers.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    dump_masters_state
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

#### list_tablet_server_log_locations

List the locations of the tablet server logs.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_tablet_server_log_locations
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

#### list_tablets_for_tablet_server

Lists all tablets for the specified tablet server (YB-TServer).

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_tablets_for_tablet_server <ts_uuid>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *ts_uuid*: The UUID of the tablet server (YB-TServer).

#### split_tablet

Splits the specified hash-sharded tablet and computes the split point as the middle of tablet's sharding range.

```sh
split_tablet -master_addresses <master-addresses> <tablet_id_to_split>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *tablet_id_to_split*: The identifier of the tablet to split.

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
    -master_addresses <master-addresses> \
    master_leader_stepdown [ <new_leader_id> ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *new_leader_id*: (Optional) The identifier (ID) of the new YB-Master leader. If not specified, the new leader is automatically elected.

#### ysql_catalog_version

Prints the current YSQL schema catalog version.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    ysql_catalog_version
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
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
    -master_addresses <master-addresses> \
    list_tables \
    [ include_db_type ] [ include_table_id ] [ include_table_type ]
```

```sh
yb-admin \
    -master_addresses <master-addresses> list_tables
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* `include_db_type`: (Optional) Add this flag to include the database type for each table.
* `include_table_id`: (Optional) Add this flag to include the table ID for each table.
* `include_table_type`: (Optional) Add this flag to include the table type for each table.

Returns tables in the following format, depending on the flags used:

```output
<db_type>.<namespace>.<table_name> table_id table_type
```

* *db_type*: The type of database. Valid values include `ysql`, `ycql`, `yedis`, and `unknown`.
* *namespace*: The name of the database (for YSQL) or keyspace (for YCQL).
* *table_name*: The name of the table.
* *table_type*: The type of table. Valid values include `catalog`, `table`, `index`, and `other`.

{{< note title="Tip" >}}

To display a list of tables and their UUID (`table_id`) values, open the **YB-Master UI** (`<master_host>:7000/`) and click **Tables** in the navigation bar.

{{< /note >}}

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
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

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    compact_table <keyspace> <table_name> \
    [timeout_in_seconds] [ADD_INDEXES]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *keyspace*: Specifies the database `ysql.db-name` or keyspace `ycql.keyspace-name`.
* *table_name*: Specifies the table name.
* *timeout_in_seconds*: Specifies duration, in seconds, yb-admin waits for compaction to end. Default value is `20`.
* *ADD_INDEXES*: Whether to compact the indexes associated with the table. Default value is `false`.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    compact_table ycql.kong test
```

```output
Started compaction of table kong.test
Compaction request id: 75c406c1d2964487985f9c852a8ef2a3
Waiting for compaction...
Compaction complete: SUCCESS
```

#### compact_table_by_id

Triggers manual compaction on a table.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    compact_table_by_id <table_id> \
    [timeout_in_seconds] [ADD_INDEXES]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *table_id*: The unique UUID associated with the table to be compacted.
* *timeout_in_seconds*: Specifies duration, in seconds, yb-admin waits for compaction to end. Default value is `20`.
* *ADD_INDEXES*: Whether to compact the indexes associated with the table. Default value is `false`.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    compact_table_by_id 000033f100003000800000000000410a
```

```output
Compacted [000033f100003000800000000000410a] tables.
```

#### modify_table_placement_info

Modifies the placement information (cloud, region, and zone) for a table.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    modify_table_placement_info <keyspace> <table_name> <placement_info> <replication_factor> \
    [ <placement_id> ]
```

or alternatively:

```sh
yb-admin \
    -master_addresses <master-addresses> \
    modify_table_placement_info tableid.<table_id> <placement_info> <replication_factor> \
    [ <placement_id> ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *keyspace*: The namespace, or name of the database or keyspace.
* *table_name*: The name of the table.
* *table_id*: The unique UUID associated with the table whose placement policy is being changed.
* *placement_info*: Comma-delimited list of placements for *cloud*.*region*.*zone*. Default value is `cloud1.datacenter1.rack1`.
* *replication_factor*: The number of replicas for each tablet.
* *placement_id*: Identifier of the primary cluster. Optional. If set, it has to match the `placement_id` specified for the primary cluster in the cluster configuration.

**Example**

```sh
./bin/yb-admin \
    -master_addresses $MASTER_RPC_ADDRS \
    modify_table_placement_info  testdatabase testtable \
    aws.us-west.us-west-2a,aws.us-west.us-west-2b,aws.us-west.us-west-2c 3
```

Verify this in the Master UI by opening the **YB-Master UI** (`<master_host>:7000/`) and clicking **Tables** in the navigation bar. Navigate to the appropriate table whose placement information you're changing, and check the Replication Info section.

{{< note title="Notes" >}}

Setting placement for tables is not supported for clusters with read-replicas or leader affinity policies enabled.

Use this command to create custom placement policies only for YCQL tables or transaction status tables. For YSQL tables, use [Tablespaces](../../explore/ysql-language-features/tablespaces) instead.
{{< /note >}}

#### create_transaction_table

Creates a transaction status table to be used in a region. This command should always be followed by [`modify_table_placement_info`](#modify-table-placement-info) to set the placement information for the newly-created transaction status table.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    create_transaction_table \
    <table_name>
```

* *master_addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *table_name*: The name of the transaction status table to be created; this must start with `transactions_`.

The transaction status table will be created as `system.<table_name>`.

**Example**

```sh
./bin/yb-admin \
    -master_addresses $MASTER_RPC_ADDRS \
    create_transaction_table \
    transactions_us_east
```

Verify this in the Master UI by opening the **YB-Master UI** (`<master_host>:7000/`) and clicking **Tables** in the navigation bar. You should see a new system table with keyspace `system` and table name `transactions_us_east`.

Next, set the placement on the newly created transactions table:

```sh
./bin/yb-admin \
    -master_addresses $MASTER_RPC_ADDRS \
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
    -master_addresses <master-addresses> \
    add_transaction_tablet \
    <table_id>
```

* *master_addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *table_id*: The identifier (ID) of the table.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    add_transaction_tablet 000033eb000030008000000000004002
```

To verify that the new status tablet has been created, run the [`list_tablets`](#list-tablets) command.

#### flush_table

Flush the memstores of the specified table on all tablet servers to disk.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    flush_table <table> | <db_type>.<namespace> [timeout_in_seconds] [ADD_INDEXES]
```

* *master_addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *db_type*: The type of database. Valid values include ysql, ycql, yedis, and unknown.
* *namespace*: The name of the database (for YSQL) or keyspace (for YCQL).
* *table*: The name of the table to flush.
* *timeout_in_seconds*: Specifies duration, in seconds when the CLI timeouts waiting for flushing to end. Default value is `20`.
* *ADD_INDEXES*: If the database should also flush the secondary indexes associated with the table. Default is `false`.

**Example**

```sh
./bin/yb-admin \
    -master_addresses $MASTER_RPC_ADDRS \
    flush_table ysql.yugabyte table_name

```
```output
Flushed [yugabyte.table_name] tables.
```
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
    -master_addresses <master-addresses> \
    create_database_snapshot <database_name>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *database*: The name of the YSQL database.

When this command runs, a `snapshot_id` is generated and printed.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    create_database_snapshot
```

To see if the database snapshot creation has completed, run the [`yb-admin list_snapshots`](#list_snapshots) command.

#### create_keyspace_snapshot

Creates a snapshot of the specified YCQL keyspace.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    create_keyspace_snapshot <keyspace_name>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *keyspace*: The name of the YCQL keyspace.

When this command runs, a `snapshot_id` is generated and printed.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    create_keyspace_snapshot
```

To see if the database snapshot creation has completed, run the [`yb-admin list_snapshots`](#list_snapshots) command.

#### list_snapshots

Prints a list of all snapshot IDs, restoration IDs, and states. Optionally, prints details (including keyspaces, tables, and indexes) in JSON format.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_snapshots \
    [ show_details ] [ not_show_restored ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* `show_details`: (Optional) Print snapshot details, including the keyspaces, tables, and indexes.
* `not_show_restored`: (Optional) Do not show successful "restorations" (that is, `COMPLETE`). Use to see a list of only uncompleted or failed restore operations.
* `show_deleted`: (Optional) Show snapshots that are deleted, but still retained in memory.

Possible `state` values for creating and restoring snapshots:

* `create_snapshot`: `CREATING`, `COMPLETE`, `DELETING`, `DELETED`, or `FAILED`.
* `restore_snapshot`: `COMPLETE`, `DELETING`, `DELETED`, or `FAILED`.

By default, the `list_snapshot` command prints the current state of the following operations:

* `create_snapshot`: `snapshot_id`, `keyspace`, `table`,  `state`
* `restore_snapshot`: `snapshot_id`, `restoration_id`,  `state`.
* `delete_snapshot`: `snapshot_id`,  `state`.

When `show_details` is included, the `list_snapshot` command prints the following details in JSON format:

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
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
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

Snapshots you create via `create_snapshot` persist on disk until you remove them using the [`delete_snapshot`](#delete-snapshot) command.

Use the [`create_snapshot_schedule`](#create-snapshot-schedule) command to create snapshots that expire after a specified time interval.

{{</ note >}}

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    create_snapshot <keyspace> <table_name> | <table_id> \
    [<keyspace> <table_name> | <table_id> ]... \
    [flush_timeout_in_seconds]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *keyspace*: The name of the database or keyspace formatted as <ycql|ysql|yedis>.<keyspace>.
* *table_name*: The name of the table name.
* *table_id*: The identifier (ID) of the table.
* *flush_timeout_in_seconds*: Specifies duration, in seconds, before flushing snapshot. Default value is `60`. To skip flushing, set the value to `0`.

When this command runs, a `snapshot_id` is generated and printed.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    create_snapshot ydb test_tb
```

```output
Started flushing table ydb.test_tb
Flush request id: fe0db953a7a5416c90f01b1e11a36d24
Waiting for flushing...
Flushing complete: SUCCESS
Started snapshot creation: 4963ed18fc1e4f1ba38c8fcf4058b295
```

To see if the snapshot creation has finished, run the [`yb-admin list_snapshots`](#list_snapshots) command.

#### restore_snapshot

Restores the specified snapshot, including the tables and indexes. When the operation starts, a `restoration_id` is generated.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    restore_snapshot <snapshot_id> <restore-target>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *snapshot_id*: The identifier (ID) for the snapshot.
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

To see if the snapshot was successfully restored, you can run the [`yb-admin list_snapshots`](#list-snapshots) command.

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
    -master_addresses <master-addresses> \
    list_snapshot_restorations <restoration_id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *restoration_id*: the snapshot restoration's unique identifier. The ID is optional; omit the ID to return all restorations in the system.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
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
    -master_addresses <master-addresses> \
    export_snapshot <snapshot_id> <file_name>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *snapshot_id*: The identifier (ID) for the snapshot.
* *file_name*: The name of the file to contain the metadata. Recommended file extension is `.snapshot`.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
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
    -master_addresses <master-addresses> \
    import_snapshot <file_name> \
    [<keyspace> <table_name> [<keyspace> <table_name>]...]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *file_name*: The name of the snapshot file to import
* *keyspace*: The name of the database or keyspace
* *table_name*: The name of the table

{{< note title="Note" >}}

The *keyspace* and the *table* can be different from the exported one.

{{< /note >}}

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
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
    -master_addresses <master-addresses> \
    import_snapshot_selective <file_name> \
    [<keyspace> <table_name> [<keyspace> <table_name>]...]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *file_name*: The name of the snapshot file to import
* *keyspace*: The name of the database or keyspace
* *table_name*: The name of the table

{{< note title="Note" >}}

The *keyspace* can be different from the exported one. The name of the table needs to be the same.

{{< /note >}}

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
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
    -master_addresses <master-addresses> \
    delete_snapshot <snapshot_id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *snapshot_id*: The identifier (ID) of the snapshot.

#### create_snapshot_schedule

Creates a snapshot schedule. A schedule consists of a list of objects to be included in a snapshot, a time interval at which to take snapshots for them, and a retention time.

Returns a schedule ID in JSON format.

**Syntax**

```sh
yb-admin create_snapshot_schedule \
    -master_addresses <master-addresses> \
    <snapshot-interval>\
    <retention-time>\
    <filter-expression>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *snapshot-interval*: The frequency at which to take snapshots, in minutes.
* *retention-time*: The number of minutes to keep a snapshot before deleting it.
* *filter-expression*: The set of objects to include in the snapshot.

The filter expression is a list of acceptable objects, which can be either raw tables, or keyspaces (YCQL) or databases (YSQL). For proper consistency guarantees, **it is recommended to set this up on a per-keyspace (YCQL) or per-database (YSQL) level**.

**Example**

Take a snapshot of the `ysql.yugabyte` database once per minute, and retain each snapshot for 10 minutes:

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    create_snapshot_schedule 1 10 ysql.yugabyte
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
    -master_addresses <master-addresses> \
    list_snapshot_schedules <schedule-id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *schedule-id*: the snapshot schedule's unique identifier. The ID is optional; omit the ID to return all schedules in the system.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
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

#### restore_snapshot_schedule

Schedules group a set of items into a single tracking object (the *schedule*). When you restore, you can choose a particular schedule and a point in time, and revert the state of all affected objects back to the chosen time.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    restore_snapshot_schedule <schedule-id> <restore-target>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
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
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    restore_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 1617670679185100
```

Restore from a relative time:

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
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
    -master_addresses <master-addresses> \
    delete_snapshot_schedule <schedule-id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *schedule-id*: the snapshot schedule's unique identifier.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
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
    -master_addresses <master-addresses> \
    modify_placement_info <placement_info> <replication_factor> \
    [ <placement_id> ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *placement_info*: Comma-delimited list of placements for *cloud*.*region*.*zone*. Optionally, after each placement block, we can also specify a minimum replica count separated by a colon. This count indicates how many minimum replicas of each tablet we want in that placement block. Its default value is 1. It is not recommended to repeat the same placement multiple times but instead specify the total count after the colon. However, in the event that the user specifies a placement multiple times, the total count from all mentions is taken.
* *replication_factor*: The number of replicas for each tablet. This value should be greater than or equal to the total of replica counts specified in *placement_info*.
* *placement_id*: The identifier of the primary cluster, which can be any unique string. Optional. If not set, a randomly-generated ID will be used.

**Example**

```sh
./bin/yb-admin \
    -master_addresses $MASTER_RPC_ADDRS \
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

#### set_preferred_zones

Sets the preferred availability zones (AZs) and regions. Tablet leaders are placed in alive and healthy nodes of AZs in order of preference. When no healthy node is available in the most preferred AZs (preference value 1), then alive and healthy nodes from the next preferred AZs are picked. AZs with no preference are equally eligible to host tablet leaders.

Having all tablet leaders reside in a single region reduces the number of network hops for the database to write transactions, which increases performance and reduces latency.

{{< note title="Note" >}}

* Make sure you've already run [`modify_placement_info`](#modify-placement-info) command beforehand.

* By default, the transaction status tablet leaders don't respect these preferred zones and are balanced across all nodes. Transactions include a roundtrip from the user to the transaction status tablet serving the transaction - using the leader closest to the user rather than forcing a roundtrip to the preferred zone improves performance.

* Leader blacklisted nodes don't host any leaders irrespective of their preference.

* Cluster configuration stores preferred zones in either affinitized_leaders or multi_affinitized_leaders object.

* Tablespaces don't inherit cluster-level placement information, leader preference, or read replica configurations.

* If the client application uses a smart driver, set the [topology keys](../../drivers-orms/smart-drivers/#topology-aware-connection-load-balancing) to target the preferred zones.

{{< /note >}}

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    set_preferred_zones <cloud.region.zone>[:preference] \
    [<cloud.region.zone>[:preference]]...
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *cloud.region.zone*: Specifies the cloud, region, and zone. Default value is `cloud1.datacenter1.rack1`.
* *preference*: Specifies the leader preference for a zone. Values have to be contiguous non-zero integers. Multiple zones can have the same value. Default value is 1.

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
    -master_addresses <master-addresses> \
    add_read_replica_placement_info <placement_info> \
    <replication_factor> \
    [ <placement_id> ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *placement_info*: A comma-delimited list of read replica placements for *cloud*.*region*.*zone*, using the format `<cloud1.region1.zone1>:<num_replicas_in_zone1>,<cloud2.region2.zone2>:<num_replicas_in_zone2>,...`. Default value is `cloud1.datacenter1.rack1`.
    Read replica availability zones must be uniquely different from the primary availability zones. To use the same cloud, region, and availability zone for a read replica as a primary cluster, you can suffix the zone with `_rr` (for read replica). For example, `c1.r1.z1` vs `c1.r1.z1_rr:1`.
* *replication_factor*: The total number of read replicas.
* *placement_id*: The identifier of the read replica cluster, which can be any unique string. If not set, a randomly-generated ID will be used. Primary and read replica clusters must use different placement IDs.

#### modify_read_replica_placement_info

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    modify_read_replica_placement_info <placement_info> \
    <replication_factor> \
    [ <placement_id> ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *placement_info*: A comma-delimited list of placements for *cloud*.*region*.*zone*. Default value is `cloud1.datacenter1.rack1`.
* *replication_factor*: The number of replicas.
* *placement_id*: The identifier of the read replica cluster, which can be any unique string. If not set, a randomly-generated ID will be used. Primary and read replica clusters must use different placement IDs.

#### delete_read_replica_placement_info

Delete the read replica.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    delete_read_replica_placement_info [ <placement_id> ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *placement_id*: The identifier of the read replica cluster, which can be any unique string. If not set, a randomly-generated ID will be used. Primary and read replica clusters must use different placement IDs.

---

### Security commands

#### Encryption at rest commands

For details on using encryption at rest, see [Encryption at rest](../../secure/encryption-at-rest).

#### add_universe_key_to_all_masters

Sets the contents of `key_path` in-memory on each YB-Master node.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    add_universe_key_to_all_masters <key_id> <key_path>
```

* *key_id*: Universe-unique identifier (can be any string, such as a string of a UUID) that will be associated to the universe key contained in the contents of `key_path` as a byte[].
* *key_path*:  The path to the file containing the universe key.

{{< note title="Note" >}}

After adding the universe keys to all YB-Master nodes, you can verify the keys exist using the `yb-admin` [`all_masters_have_universe_key_in_memory`](#all-masters-have-universe-key-in-memory) command and enable encryption using the [`rotate_universe_key_in_memory`](#rotate-universe-key-in-memory) command.

{{< /note >}}

#### all_masters_have_universe_key_in_memory

Checks whether the universe key associated with the provided *key_id* exists in-memory on each YB-Master node.

```sh
yb-admin \
    -master_addresses <master-addresses> all_masters_have_universe_key_in_memory <key_id>
```

* *key_id*: Universe-unique identifier (can be any string, such as a string of a UUID) that will be associated to the universe key contained in the contents of `key_path` as a byte[].

#### rotate_universe_key_in_memory

Rotates the in-memory universe key to start encrypting newly-written data files with the universe key associated with the provided `key_id`.

{{< note title="Note" >}}

The [`all_masters_have_universe_key_in_memory`](#all-masters-have-universe-key-in-memory) value must be true for the universe key to be successfully rotated and enabled).

{{< /note >}}

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> rotate_universe_key_in_memory <key_id>
```

* *key_id*: Universe-unique identifier (can be any string, such as a string of a UUID) that will be associated to the universe key contained in the contents of `key_path` as a byte[].

#### disable_encryption_in_memory

Disables the in-memory encryption at rest for newly-written data files.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    disable_encryption_in_memory
```

#### is_encryption_enabled

Checks if cluster-wide encryption is enabled.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    is_encryption_enabled
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

Returns message:

```output
Encryption status: ENABLED with key id <key_id_2>
```

The new key ID (`<key_id_2>`) should be different from the previous one (`<key_id>`).

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    is_encryption_enabled
```

```output
Encryption status: ENABLED with key id <key_id_2>
```

### Change Data Capture (CDC) commands

#### create_change_data_stream

Creates a change data capture (CDC) DB stream for the specified table.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    create_change_data_stream ysql.<namespace_name>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *namespace_name*: The namespace on which the DB stream ID is to be created.

For example:

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.1:7100 \
    create_change_data_stream ysql.yugabyte
```



##### Enabling before image

To create a change data capture (CDC) DB stream which also supports sending the before image of the record, use the following command.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    create_change_data_stream ysql.<namespace_name> IMPLICIT ALL
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *namespace_name*: The namespace on which the DB stream ID is to be created.
* `IMPLICIT`: Checkpointing type on the server.
* `ALL`: Record type indicating the server that the stream should send the before image too.

A successful operation of the above command returns a message with a DB stream ID:

```output
CDC Stream ID: d540f5e4890c4d3b812933cbfd703ed3
```

##### Creating stream in EXPLICIT checkpointing mode

To create a change data capture (CDC) DB stream which works in the EXPLICIT checkpointing mode where the client is responsible for managing the checkpoints, use the following command:

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    create_change_data_stream ysql.<namespace_name> EXPLICIT
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *namespace_name*: The namespace on which the DB stream ID is to be created.
* `EXPLICIT`: Checkpointing type on the server.

A successful operation of the above command returns a message with a DB stream ID:

```output
CDC Stream ID: d540f5e4890c4d3b812933cbfd703ed3
```

#### list_change_data_streams

Lists all the created CDC DB streams.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_change_data_streams [namespace_name]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *namespace_name*: Optional - The namespace name for which the streams are to be listed, if not provided it would list all the streams without filtering.

**Example:**

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.1:7100 \
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
    -master_addresses <master-addresses> \
    get_change_data_stream_info <db_stream_id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *db_stream_id*: The CDC DB stream ID to get the info of.

**Example:**

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.1:7100 \
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
    -master_addresses <master-addresses> \
    delete_change_data_stream <db_stream_id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *db_stream_id*: The CDC DB stream ID to be deleted.

**Example:**

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.1:7100 \
    delete_change_data_stream d540f5e4890c4d3b812933cbfd703ed3
```

The above command results in the following response:

```output
Successfully deleted CDC DB Stream ID: d540f5e4890c4d3b812933cbfd703ed3
```

### xCluster Replication commands

#### setup_universe_replication

Sets up the universe replication for the specified source universe. Use this command only if no tables have been configured for replication. If tables are already configured for replication, use [alter_universe_replication](#alter-universe-replication) to add more tables.

To verify if any tables are already configured for replication, use [list_cdc_streams](#list-cdc-streams).

**Syntax**

```sh
yb-admin \
    -master_addresses <target_master_addresses> \
    setup_universe_replication \
    <source_universe_uuid>_<replication_name> \
    <source_master_addresses> \
    <comma_separated_list_of_table_ids> \
    [ <comma_separated_list_of_producer_bootstrap_ids> ] \
    [ transactional ]
```

* *target_master_addresses*: Comma-separated list of target YB-Master hosts and ports. Default value is `localhost:7100`.
* *source_universe_uuid*: The UUID of the source universe.
* *replication_name*: The name for the replication.
* *source_master_addresses*: Comma-separated list of the source master addresses.
* *comma_separated_list_of_table_ids*: Comma-separated list of source universe table identifiers (`table_id`).
* *comma_separated_list_of_producer_bootstrap_ids*: Comma-separated list of source universe bootstrap identifiers (`bootstrap_id`). Obtain these with [bootstrap_cdc_producer](#bootstrap-cdc-producer-comma-separated-list-of-table-ids), using a comma-separated list of source universe table IDs.
* *transactional*: identifies the universe as Active in a transactional xCluster deployment.

{{< warning title="Important" >}}
Enter the source universe bootstrap IDs in the same order as their corresponding table IDs.
{{< /warning >}}

{{< note title="Tip" >}}

To display a list of tables and their UUID (`table_id`) values, open the **YB-Master UI** (`<master_host>:7000/`) and click **Tables** in the navigation bar.

{{< /note >}}

**Example**

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
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
yb-admin -master_addresses <target_master_addresses> \
    alter_universe_replication <source_universe_uuid>_<replication_name> \
    set_master_addresses <source_master_addresses>
```

* *target_master_addresses*: Comma-separated list of target YB-Master hosts and ports. Default value is `localhost:7100`.
* *source_universe_uuid*: The UUID of the source universe.
* *replication_name*: The name of the replication to be altered.
* *source_master_addresses*: Comma-separated list of the source master addresses.

Use the `add_table` subcommand to add one or more tables to the existing list:

```sh
yb-admin -master_addresses <target_master_addresses> \
    alter_universe_replication <source_universe_uuid>_<replication_name> \
    add_table [ <comma_separated_list_of_table_ids> ] \
    [ <comma_separated_list_of_producer_bootstrap_ids> ]
```

* *target_master_addresses*: Comma-separated list of target YB-Master hosts and ports. Default value is `localhost:7100`.
* *source_universe_uuid*: The UUID of the source universe.
* *replication_name*: The name of the replication to be altered.
* *comma_separated_list_of_table_ids*: Comma-separated list of source universe table identifiers (`table_id`).
* *comma_separated_list_of_producer_bootstrap_ids*: Comma-separated list of source universe bootstrap identifiers (`bootstrap_id`). Obtain these with [bootstrap_cdc_producer](#bootstrap-cdc-producer-comma-separated-list-of-table-ids), using a comma-separated list of source universe table IDs.

{{< warning title="Important" >}}
Enter the source universe bootstrap IDs in the same order as their corresponding table IDs.
{{< /warning >}}

Use the `remove_table` subcommand to remove one or more tables from the existing list:

```sh
yb-admin -master_addresses <target_master_addresses> \
    alter_universe_replication <source_universe_uuid>_<replication_name> \
    remove_table [ <comma_separated_list_of_table_ids> ]
```

* *target_master_addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *source_universe_uuid*: The UUID of the source universe.
* *replication_name*: The name of the replication to be altered.
* *comma_separated_list_of_table_ids*: Comma-separated list of source universe table identifiers (`table_id`).

Use the `rename_id` subcommand to rename xCluster replication streams.

```sh
yb-admin -master_addresses <target_master_addresses> \
    alter_universe_replication <source_universe_uuid>_<replication_name> \
    rename_id <source_universe_uuid>_<new_replication_name>
```

* *target_master_addresses*: Comma-separated list of target YB-Master hosts and ports. Default value is `localhost:7100`.
* *source_universe_uuid*: The UUID of the source universe.
* *replication_name*: The name of the replication to be altered.
* *new_replication_name*: The new name of the replication stream.

#### delete_universe_replication <source_universe_uuid>

Deletes universe replication for the specified source universe.

**Syntax**

```sh
yb-admin \
    -master_addresses <target_master_addresses> \
    delete_universe_replication <source_universe_uuid>_<replication_name>
```

* *target_master_addresses*: Comma-separated list of target YB-Master hosts and ports. Default value is `localhost:7100`.
* *source_universe_uuid*: The UUID of the source universe.
* *replication_name*: The name of the replication to be deleted.

#### set_universe_replication_enabled

Sets the universe replication to be enabled or disabled.

**Syntax**

```sh
yb-admin \
    -master_addresses <target_master_addresses> \
    set_universe_replication_enabled <source_universe_uuid>_<replication_name>
```

* *target_master_addresses*: Comma-separated list of target YB-Master hosts and ports. Default value is `localhost:7100`.
* *source_universe_uuid*: The UUID of the source universe.
* *replication_name*: The name of the replication to be enabled or disabled.
* `0` | `1`: Disabled (`0`) or enabled (`1`). Default is `1`.

#### change_xcluster_role

Sets the xCluster role to `STANDBY` or `ACTIVE`.

**Syntax**

```sh
yb-admin \
    -master_addresses <master_addresses> \
    change_xcluster_role \
    <role>
```

* *master_addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`. These are the addresses of the master nodes where the role is to be applied. For example, to change the target to `STANDBY`, use target universe master addresses, and to change the source universe role, use source universe master addresses.
* *role*: Can be `STANDBY` or `ACTIVE`.

**Example**

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
    change_xcluster_role STANDBY
```

#### get_xcluster_safe_time

Reports the current xCluster safe time for each namespace, which is the time at which reads will be performed.

**Syntax**

```sh
yb-admin \
    -master_addresses <target_master_addresses> \
    get_xcluster_safe_time \
    [include_lag_and_skew]
```

* *target_master_addresses*: Comma-separated list of target YB-Master hosts and ports. Default value is `localhost:7100`.
* *include_lag_and_skew*: Set `include_lag_and_skew` option to show `safe_time_lag_sec` and `safe_time_skew_sec`, otherwise these are hidden by default.

**Example**

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
    get_xcluster_safe_time
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
    -master_addresses <source_master_addresses> \
    wait_for_replication_drain \
    <comma_separated_list_of_stream_ids> [<timestamp> | minus <interval>]
```

* *source_master_addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *comma_separated_list_of_stream_ids*: Comma-separated list of stream IDs.
* *timestamp*: The time to which to wait for replication to drain. If not provided, it will be set to current time in the YB-Master API.
* *minus <interval>*: The `minus <interval>` is the same format as described in [Restore from a relative time](../../explore/cluster-management/point-in-time-recovery-ysql/#restore-from-a-relative-time), or see [`restore_snapshot_schedule`](#restore-snapshot-schedule).

**Example**

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
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

Lists the CDC streams for the specified YB-Master servers.

{{< note title="Tip" >}}

Use this command when setting up universe replication to verify if any tables are configured for replication. If not, run [`setup_universe_replication`](#setup-universe-replication); if tables are already configured for replication, use [`alter_universe_replication`](#alter-universe-replication) to add more tables.

{{< /note >}}

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_cdc_streams
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

**Example**

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
    list_cdc_streams
```

#### delete_cdc_stream <stream_id> [force_delete]

Deletes underlying CDC stream for the specified YB-Master servers.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    delete_cdc_stream <stream_id [force_delete]>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *stream_id*: The ID of the CDC stream.
* `force_delete`: (Optional) Force the delete operation.

{{< note title="Note" >}}
This command should only be needed for advanced operations, such as doing manual cleanup of old bootstrapped streams that were never fully initialized, or otherwise failed replication streams. For normal xCluster replication cleanup, use [delete_universe_replication](#delete-universe-replication).
{{< /note >}}

#### bootstrap_cdc_producer <comma_separated_list_of_table_ids>

Mark a set of tables in preparation for setting up universe level replication.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    bootstrap_cdc_producer <comma_separated_list_of_table_ids>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *comma_separated_list_of_table_ids*: Comma-separated list of table identifiers (`table_id`).

**Example**

```sh
./bin/yb-admin \
    -master_addresses 172.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
    bootstrap_cdc_producer 000030ad000030008000000000004000
```

```output
table id: 000030ad000030008000000000004000, CDC bootstrap id: dd5ea73b5d384b2c9ebd6c7b6d05972c
```

{{< note title="Note" >}}
The CDC bootstrap ids are the ones that should be used with [`setup_universe_replication`](#setup-universe-replication) and [`alter_universe_replication`](#alter-universe-replication).
{{< /note >}}

#### get_replication_status

Returns the replication status of all consumer streams. If *source_universe_uuid* is provided, this will only return streams that belong to an associated universe key. If *replication_name* is provided, this will only return the stream that belongs to the specified replication.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    get_replication_status [ <source_universe_uuid>_<replication_name> ]
```

* *source_universe_uuid*: (Optional) The UUID of the source universe.
* *replication_name*: (Optional) The name of the replication stream.

**Example**

```sh
./bin/yb-admin \
    -master_addresses 172.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
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

---

### Decommissioning commands

#### get_leader_blacklist_completion

Gets the tablet load move completion percentage for blacklisted nodes.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    get_leader_blacklist_completion
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    get_leader_blacklist_completion
```

#### change_blacklist

Changes the blacklist for YB-TServer servers.

After old YB-TServer servers are terminated, you can use this command to clean up the blacklist.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    change_blacklist [ ADD | REMOVE ] <ip_addr>:<port> \
    [ <ip_addr>:<port> ]...
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* ADD | REMOVE: Adds or removes the specified YB-TServer server from blacklist.
* *ip_addr:port*: The IP address and port of the YB-TServer.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    change_blacklist \
      ADD node1:9100 node2:9100 node3:9100 node4:9100 node5:9100 node6:9100
```

#### change_leader_blacklist

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    change_leader_blacklist [ ADD | REMOVE ] <ip_addr>:<port> \
    [ <ip_addr>:<port> ]...
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* ADD | REMOVE: Adds or removes the specified YB-TServer from leader blacklist.
* *ip_addr:port*: The IP address and port of the YB-TServer.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
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
    -master_addresses <master-addresses> \
    leader_stepdown <tablet_id> <dest_ts_uuid>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *tablet_id*: The identifier (ID) of the tablet.
* *dest_ts_uuid*: The destination identifier (UUID) for the new YB-TServer leader. To move leadership **from** the current leader, when you do not need to specify a new leader, use `""` for the value. If you want to transfer leadership intentionally **to** a specific new leader, then specify the new leader.

{{< note title="Note" >}}

If specified, `des_ts_uuid` becomes the new leader. If the argument is empty (`""`), then a new leader will be elected automatically. In a future release, this argument will be optional. See GitHub issue [#4722](https://github.com/yugabyte/yugabyte-db/issues/4722)

{{< /note >}}

---

### Rebalancing commands

For information on YB-Master load balancing, see [Data placement and load balancing](../../architecture/concepts/yb-master/#data-placement-and-load-balancing)

For YB-Master load balancing flags, see [Load balancing flags](../../reference/configuration/yb-master/#load-balancing-flags).

#### set_load_balancer_enabled

Enables or disables the load balancer.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    set_load_balancer_enabled [ 0 | 1 ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* `0` | `1`: Enabled (`1`) is the default. To disable, set to `0`.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    set_load_balancer_enabled 0
```

#### get_load_balancer_state

Returns the cluster load balancer state.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> get_load_balancer_state
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

#### get_load_move_completion

Checks the percentage completion of the data move.

You can rerun this command periodically until the value reaches `100.0`, indicating that the data move has completed.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    get_load_move_completion
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

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
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
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
    -master_addresses <master-addresses> \
    get_is_load_balancer_idle
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    get_is_load_balancer_idle
```

---

### Upgrade

Refer to [Upgrade a deployment](../../manage/upgrade-deployment/) to learn about how to upgrade a YugabyteDB cluster.

#### promote_auto_flags

[AutoFlags](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/auto_flags.md) protect new features that modify the format of data sent over the wire or stored on-disk. After all YugabyteDB processes have been upgraded to the new version, these features can be enabled by promoting their AutoFlags.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    promote_auto_flags \
    [<max_flags_class> [<promote_non_runtime_flags> [force]]]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *max_flags_class*: The maximum AutoFlag class to promote. Allowed values are `kLocalVolatile`, `kLocalPersisted`, `kExternal`, `kNewInstallsOnly`. Default value is `kExternal`.
* *promote_non_runtime_flags*: Weather to promote non-runtime flags. Allowed values are `true` and `false`. Default value is `true`.
* *force*: Forces the generation of a new AutoFlag configuration and sends it to all YugabyteDB processes even if there are no new AutoFlags to promote.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
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

YSQL upgrades are not required for clusters where [YSQL is not enabled](../../reference/configuration/yb-tserver/#ysql-flags).

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    upgrade_ysql
```

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    upgrade_ysql
```

A successful upgrade returns the following message:

```output
YSQL successfully upgraded to the latest version
```

In certain scenarios, a YSQL upgrade can take longer than 60 seconds, which is the default timeout value for `yb-admin`. To account for that, run the command with a higher timeout value:

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    -timeout_ms 180000 \
    upgrade_ysql
```

Running the above command is an online operation and doesn't require stopping a running cluster. This command is idempotent and can be run multiple times without any side effects.

{{< note title="Note" >}}
Concurrent operations in a cluster can lead to various transactional conflicts, catalog version mismatches, and read restart errors. This is expected, and should be addressed by rerunning the upgrade command.
{{< /note >}}

---
title: yb-admin - Table commands
headerTitle: yb-admin Table commands
linkTitle: Table commands
description: yb-admin Table commands.
menu:
  preview:
    identifier: yb-admin-table
    parent: yb-admin
    weight: 20
type: docs
---

## list_tables

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

## compact_table

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

## compact_table_by_id

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

## modify_table_placement_info

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

## create_transaction_table

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

## add_transaction_tablet

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

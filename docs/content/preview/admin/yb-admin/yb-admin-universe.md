---
title: yb-admin - Universe commands
headerTitle: yb-admin command reference
linkTitle: Universe
description: yb-admin Universe commands.
headcontent: Universe and cluster commands
menu:
  preview:
    identifier: yb-admin-universe
    parent: yb-admin
    weight: 10
type: docs
---

## Contents

* [Universe and cluster commands](#universe-and-cluster-commands)
* [Table commands](#table-commands)
* [Deployment commands](#deployment-commands)
* [Encryption at rest commands](#encryption-at-rest-commands)
* [Decommissioning commands](#decommissioning-commands)
* [Rebalancing commands](#rebalancing-commands)
* [Upgrade commands](#upgrade)

## Universe and cluster commands

### get_universe_config

Gets the configuration for the universe.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    get_universe_config
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

### change_config

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

### change_master_config

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

### list_tablet_servers

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_tablet_servers <tablet_id>
```

* *master_addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *tablet_id*: The identifier (ID) of the tablet.

### list_tablets

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

### list_all_tablet_servers

Lists all tablet servers.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_all_tablet_servers
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

### list_all_masters

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

### list_replica_type_counts

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

### dump_masters_state

Prints the status of the YB-Master servers.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    dump_masters_state
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

### list_tablet_server_log_locations

List the locations of the tablet server logs.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_tablet_server_log_locations
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

### list_tablets_for_tablet_server

Lists all tablets for the specified tablet server (YB-TServer).

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_tablets_for_tablet_server <ts_uuid>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *ts_uuid*: The UUID of the tablet server (YB-TServer).

### split_tablet

Splits the specified hash-sharded tablet and computes the split point as the middle of tablet's sharding range.

```sh
split_tablet -master_addresses <master-addresses> <tablet_id_to_split>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *tablet_id_to_split*: The identifier of the tablet to split.

For more information on tablet splitting, see:

* [Tablet splitting](../../architecture/docdb-sharding/tablet-splitting) — Architecture overview
* [Automatic Re-sharding of Data with Tablet Splitting](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/docdb-automatic-tablet-splitting.md) — Architecture design document in the GitHub repository.

### master_leader_stepdown

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

### ysql_catalog_version

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

## Table commands

### list_tables

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

### compact_table

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

### compact_table_by_id

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

### modify_table_placement_info

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

### create_transaction_table

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

### add_transaction_tablet

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

## Deployment commands

### Multi-zone and multi-region

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

Here is a sample configuration:

```yaml.output
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

```yaml.output
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

### Read replica

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
* *placement_info*: A comma-delimited list of placements for *cloud*.*region*.*zone*. Default value is `cloud1.datacenter1.rack1`.
* *replication_factor*: The number of replicas.
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

## Encryption at rest commands

For details on using encryption at rest, see [Encryption at rest](../../secure/encryption-at-rest).

### add_universe_key_to_all_masters

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

### all_masters_have_universe_key_in_memory

Checks whether the universe key associated with the provided *key_id* exists in-memory on each YB-Master node.

```sh
yb-admin \
    -master_addresses <master-addresses> all_masters_have_universe_key_in_memory <key_id>
```

* *key_id*: Universe-unique identifier (can be any string, such as a string of a UUID) that will be associated to the universe key contained in the contents of `key_path` as a byte[].

### rotate_universe_key_in_memory

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

### disable_encryption_in_memory

Disables the in-memory encryption at rest for newly-written data files.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    disable_encryption_in_memory
```

### is_encryption_enabled

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

## Decommissioning commands

### get_leader_blacklist_completion

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

### change_blacklist

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

### change_leader_blacklist

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

### leader_stepdown

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

## Rebalancing commands

For information on YB-Master load balancing, see [Data placement and load balancing](../../architecture/concepts/yb-master/#data-placement-and-load-balancing)

For YB-Master load balancing flags, see [Load balancing flags](../../reference/configuration/yb-master/#load-balancing-flags).

### set_load_balancer_enabled

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

### get_load_balancer_state

Returns the cluster load balancer state.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> get_load_balancer_state
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

### get_load_move_completion

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

### get_is_load_balancer_idle

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

## Upgrade

Refer to [Upgrade a deployment](../../manage/upgrade-deployment/) to learn about how to upgrade a YugabyteDB cluster.

### promote_auto_flags

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

### upgrade_ysql

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

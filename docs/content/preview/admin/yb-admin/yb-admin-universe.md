---
title: yb-admin - Universe commands
headerTitle: yb-admin Universe and cluster commands
linkTitle: Universe commands
description: yb-admin Universe commands.
menu:
  preview:
    identifier: yb-admin-universe
    parent: yb-admin
    weight: 10
type: docs
---

## get_universe_config

Gets the configuration for the universe.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    get_universe_config
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

## change_config

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

## change_master_config

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

## list_tablet_servers

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_tablet_servers <tablet_id>
```

* *master_addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *tablet_id*: The identifier (ID) of the tablet.

## list_tablets

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

## list_all_tablet_servers

Lists all tablet servers.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_all_tablet_servers
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

## list_all_masters

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

## list_replica_type_counts

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

## dump_masters_state

Prints the status of the YB-Master servers.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    dump_masters_state
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

## list_tablet_server_log_locations

List the locations of the tablet server logs.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_tablet_server_log_locations
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

## list_tablets_for_tablet_server

Lists all tablets for the specified tablet server (YB-TServer).

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_tablets_for_tablet_server <ts_uuid>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *ts_uuid*: The UUID of the tablet server (YB-TServer).

## split_tablet

Splits the specified hash-sharded tablet and computes the split point as the middle of tablet's sharding range.

```sh
split_tablet -master_addresses <master-addresses> <tablet_id_to_split>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *tablet_id_to_split*: The identifier of the tablet to split.

For more information on tablet splitting, see:

* [Tablet splitting](../../architecture/docdb-sharding/tablet-splitting) — Architecture overview
* [Automatic Re-sharding of Data with Tablet Splitting](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/docdb-automatic-tablet-splitting.md) — Architecture design document in the GitHub repository.

## master_leader_stepdown

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

## ysql_catalog_version

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

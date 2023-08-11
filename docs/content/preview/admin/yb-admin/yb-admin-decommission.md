<!---
title: yb-admin - Decommission, rebalance, and update commands
headerTitle: Decommission, rebalance, and update commands
linkTitle: Decommission, rebalance, and update commands
description: yb-admin Decommission, rebalance, and update commands.
menu:
  preview:
    identifier: yb-admin-decommission
    parent: yb-admin
    weight: 70
type: docs
--->

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

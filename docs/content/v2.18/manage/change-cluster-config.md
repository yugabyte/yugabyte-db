---
title: Change cluster configuration
headerTitle: Change cluster configuration
linkTitle: Change cluster configuration
description: Migrate a YugabyteDB cluster to a new set of nodes.
headcontent: Migrate a YugabyteDB cluster to a new set of nodes
menu:
  stable:
    identifier: manage-change-cluster-config
    parent: manage
    weight: 704
type: docs
---

Sometimes you may need to move a YugabyteDB universe deployed on a set of nodes to a completely different set of nodes. Some scenarios that require such a cluster change are:

- changing the instance or machine type.
- updating the instance images on a regular basis (for example, AMI rehydration in AWS).
- moving to a different set of zones, regions, or data centers.

This page describes how to change a cluster's configuration, including setting up new servers, moving the data from the old servers, and shutting down the old servers. This tutorial assumes that you are familiar with the [YugabyteDB architecture](../../architecture/concepts/universe/).

## Example scenario

Assume the following is the initial setup of the universe:

- Six nodes: `node1`, `node2`, `node3`, `node4`, `node5`, and `node6`, each representing the IP address of a machine/VM.
- Replication factor (RF) of three.
- Nodes `node1`, `node2`, and `node3` are the three YB-Master nodes.
- All six nodes run YB-TServers.

And you are moving to the following configuration:

- Six different nodes: `node7`, `node8`, `node9`, `node10`, `node11`, and `node12`.
- RF of three.
- Nodes `node7`, `node8`, and `node9` will be the three master nodes.
- All six nodes run YB-TServers.
- The original six nodes `node1`, `node2`, `node3`, `node4`, `node5`, and `node6` will no longer be part of this universe.

## Prerequisites

### Ensure universe is in healthy state

To ensure you don't inadvertently cause any further under replication on top of ongoing failures, verify the following:

- All YB-Master servers are running and able to talk to each other. This can be verified by checking the master UI (at *http://`node1`:7000/*) and ensure the `Masters` tab shows all three with one in the `LEADER` for `RAFT Role`.
- All the YB-TServer servers are running and heartbeating to the master leader. This can be verified by checking the master UI (at *http://`node1`:7000/tablet-servers*), which lists all the tablet servers in an `ALIVE` state.

### Ensure new machines are ready

Spin up a new set of virtual machines or servers (with the new AMI, for example) with IP addresses `node7`, `node8`, `node9`, `node10`, `node11`, and `node12`.

## 1. Configure new machines

Do the following to configure the six new machines:

- Follow the [System configuration](../../deploy/manual-deployment/system-config/) instructions for system setup.
- Install [YugabyteDB Software](../../deploy/manual-deployment/install-software/) on each new machine.

## 2. Start YB-Master servers

Run the following command to bring up the new YB-Master server on the new master nodes `node7`, `node8`, and `node9`.

```sh
~/master/bin/yb-master                \
    --fs_data_dirs <data directories> \
    >& /mnt/d0/yb-master.out &
```

{{< note title="Note" >}}
Do not set the `master_addresses` parameter for these new masters. When `master_addresses` is not set, the master server starts running without joining any existing master quorum. These nodes will be added to the master quorum in a later step.
{{< /note >}}

Refer to [starting master servers](../../deploy/manual-deployment/start-masters/) for further parameters and options.

## 3. Start YB-TServer servers

Run the following command to bring up the `tserver` servers on all the new nodes `node7`, `node8`, `node9`, `node10`, `node11`, and `node12`.

```sh
export MASTERS=node1:7100,node2:7100,node3:7100,node7:7100,node8:7100,node9:7100
~/tserver/bin/yb-tserver              \
    --tserver_master_addrs $MASTERS   \
    --fs_data_dirs <data directories> \
    >& /mnt/d0/yb-tserver.out &
```

Refer to [starting `tserver` servers](../../deploy/manual-deployment/start-tservers/) for further parameters and options.

{{< note title="Note" >}}
The `tserver_master_addrs` parameter includes the new master IP addresses as well, so that they can keep heartbeating/reporting to the new master even after the old masters are removed from master quorum.
{{< /note >}}

Now that the YB-TServer servers are running, verify that all twelve YB-TServers (six old and six new) are heartbeating to the master leader. Go to *http://`node1`:7000/tablet-servers* to confirm that twelve servers have a status of `ALIVE`.

## 4. Perform data move

The data on this cluster can now be moved. First, `blacklist` the old tablet servers to move the data away from them into the new set of tablet servers.

The following commands can be run from one of the old master nodes. You can first blacklist the six old YB-TServers:

```sh
export MASTERS=node1:7100,node2:7100,node3:7100
~/master/bin/yb-admin -master_addresses $MASTERS change_blacklist ADD node1:9100 node2:9100 node3:9100 node4:9100 node5:9100 node6:9100
```

Verify that the blacklist information looks similar to the following:

```sh
~/master/bin/yb-admin -master_addresses $MASTERS get_universe_config
Config:
version: 5
server_blacklist {
  hosts {
    host: "node1"
    port: 9100
  }
  hosts {
    host: "node2"
    port: 9100
  }
  ...
  ...
  hosts {
    host: "node6"
    port: 9100
  }
}
```

Next, wait for the data move to complete. You can check the percentage completion by running the following command:

```sh
~/master/bin/yb-admin -master_addresses $MASTERS get_load_move_completion
Percent complete = 66.6
```

In the preceding example, the data move is `66.6%` done. Re-run this command periodically until the returned value reaches `100`.

{{< note title="Note" >}}
The time needed for this data move depends on the following:

- number of tablets/tables
- size of each of those tablets
- disk/ssd transfer speeds
- network bandwidth between the new nodes and the existing ones
{{< /note >}}

## 5. Master quorum change

Now move the master quorum from the old set of masters (`node1`,`node2`, and `node3`) to the new set of masters (`node7`,`node8`, and `node9`). Do this by adding one new master and then removing one old master, in sequence, until all the old masters are removed. This can be run from one of the new masters.

The `ADD_SERVER` command adds a new master and `REMOVE_SERVER` command removes an old master from the master quorum. After every step, check the YB-Master state on the master UI (at *http://`node7`:7000*).

{{< note title="Note" >}}
If any error log is reported on the command line from the following steps, check the master UI, as the error might be transitional, and can be ignored.
{{< /note >}}

```sh
export MASTERS=node1:7100,node2:7100,node3:7100,node7:7100,node8:7100,node9:7100
~/master/bin/yb-admin -master_addresses $MASTERS change_master_config ADD_SERVER node7 7100
~/master/bin/yb-admin -master_addresses $MASTERS change_master_config REMOVE_SERVER node1 7100
~/master/bin/yb-admin -master_addresses $MASTERS change_master_config ADD_SERVER node8 7100
~/master/bin/yb-admin -master_addresses $MASTERS change_master_config REMOVE_SERVER node2 7100
~/master/bin/yb-admin -master_addresses $MASTERS change_master_config ADD_SERVER node9 7100
~/master/bin/yb-admin -master_addresses $MASTERS change_master_config REMOVE_SERVER node3 7100
```

Now ensure that the master leader is one of the new master nodes as follows:

```sh
$ export MASTERS=node7:7100,node8:7100,node9:7100
$ ~/master/bin/yb-admin -master_addresses $MASTERS list_all_masters
```

```output
Master UUID         RPC Host/Port          State      Role
...                   node8:7100           ALIVE     FOLLOWER
...                   node9:7100           ALIVE     FOLLOWER
...                   node7:7100           ALIVE     LEADER
```

And confirm the same on *http://`node7`:7000/*, that the set of master IP addresses in the `Masters` list are only the three new ones.

On a new YB-Master UI page, ensure that all the new tablet servers are reporting to the master leader and have the tablet load distributed. For example, *http://`node7`:7000/tablet-servers* should show the `Load` on six new YB-TServers. The old YB-TServer can be in `DEAD` status.

## 6. Update master addresses on YB-TServers

The `tserver_master_addrs` parameter for all the new YB-TServers needs to be set to the list of three new master IPs `node7:7100,node8:7100,node9:7100` for future use.

{{< note title="Tip" >}}
Updating master addresses is needed in case the yb-tserver server is restarted.
{{< /note >}}

## 7. Clean up

The old nodes are not part of the universe any more and can be shut down. After the old YB-TServers are terminated, you can cleanup the blacklist from the master configuration using the following command:

```sh
~/master/bin/yb-admin -master_addresses $MASTERS change_blacklist REMOVE node1:9100 node2:9100 node3:9100 node4:9100 node5:9100 node6:9100
```

{{< note title="Tip" >}}
Cleaning up the blacklist server will help reuse the older IPs in case they get recycled.
{{< /note >}}

Ensure there are no `server_blacklist` entries returned by the command:

```sh
~/master/bin/yb-admin -master_addresses $MASTERS get_universe_config
```

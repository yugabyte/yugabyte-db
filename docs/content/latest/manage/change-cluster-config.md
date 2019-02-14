---
title: Change Cluster Config
linkTitle: Change Cluster Config
description: Change Cluster Config
aliases:
  - manage/change-cluster-config
menu:
  latest:
    identifier: manage-change-cluster-config
    parent: manage
    weight: 704
isTocNested: true
showAsideToc: true
---

Sometimes there might be a need to move a YugaByte DB universe deployed on a set of nodes to a completely different set of nodes in an online manner. Some scenarios that require such a cluster change are:

- changing the instance or machine type.
- updating the instance images on a regular basis (e.g., AMI rehydration in AWS).
- moving to a different set of zones, regions or datacenters.

This page provides the steps needed to perform such a data move in an online manner from the initial setup to the final setup as described below. This tutorial assumes that you are familiar with the [YugaByte DB process architecture](../../../../architecture/concepts/universe/).

## Example scenario

### Initial config

We will assume the following about the initial setup of the universe:

- Universe has six nodes: `node1`, `node2`, `node3`, `node4`, `node5` and `node6`, each representing the IP address of a machine/VM.
- Replication factor (RF) for this universe is three.
- Nodes `node1`, `node2` and `node3` are the three master nodes.
- All six nodes run tservers.

### Desired config
We will transform the universe into the following final setup:

- The universe will have six different nodes: `node7`, `node8`, `node9`, `node10`, `node11` and `node12`.
- Replication factor (RF) is still three.
- Nodes `node7`, `node8`, `node9` will be the three master nodes.
- All six nodes run tservers.
- The original six nodes `node1`, `node2`, `node3`, `node4`, `node5` and `node6` will no longer be part of this universe.

## Prerequisites

### Ensure universe is in healthy state

This is to ensure that we do not inadvertently cause any further under replication on top of ongoing failures.

- All yb-master processes are running and able to talk to each other. This can be verified by checking the master UI (ex., *http://`node1`:7000/*) and ensure the `Masters` tab shows those three with one in the `LEADER` for `RAFT Role`.
- All the yb-tserver processes are running and heartbeating to the master leader. This can be verified by checking the master UI (ex., *http://`node1`:7000/tablet-servers*) which lists all the tablet servers in `ALIVE` state.

### Ensure new machines are ready
Spin up a new set of VMs/machines (with the new AMI, for example) with IPs `node7`, `node8`, `node9`, `node10`, `node11` and `node12`.


## 1. Configure new machines

Use these two steps to configure the six new machines:

  - Follow the [System Configuration](../../deploy/manual-deployment/system-config/) instructions for system setup.
  - Install [YugaByte Software](../../deploy/manual-deployment/install-software/) on each new machine.


## 2. Start master processes

Run the command below to bring up the new master process on the new master nodes `node7`, `node8` and `node9`. When the `master_addresses` parameter is not set, this master process starts running without joining any existing master quorum. These nodes will be added to the master quorum in a later step.

```sh
~/master/bin/yb-master                \
    --fs_data_dirs <data directories> \
    >& /mnt/d0/yb-master.out &
```

{{< note title="Note" >}}
The `master_addresses` parameter should not be set for these new masters.
{{< /note >}}


Refer to [starting master processes](../../../../deploy/manual-deployment/start-masters/) for further parameters and options.

## 3. Start tserver processes

Run the command below to bring up the tserver processes on all the new nodes `node7`, `node8`, `node9`, `node10`, `node11` and `node12`. 


```sh
export MASTERS=node1:7100,node2:7100,node3:7100,node7:7100,node8:7100,node9:7100
~/tserver/bin/yb-tserver              \
    --tserver_master_addrs $MASTERS   \
    --fs_data_dirs <data directories> \
    >& /mnt/d0/yb-tserver.out &
```
Refer to [starting tserver processes](../../../../deploy/manual-deployment/start-tservers/) for further parameters and options.

{{< note title="Note" >}}
The `tserver_master_addrs` parameter includes the new master IPs as well, so that they can keep heartbeating/reporting to the new master even after the old masters are removed from master quorum.
{{< /note >}}

Now that the tserver processes are running, we should verify that all the twelve tservers (six old and six new) are heartbeating to the master leader. Go to *http://`node1`:7000/tablet-servers* and confirm that twelve servers are in `ALIVE` status.


## 4. Perform data move
The data on this cluster can now be moved. First, we `blacklist` the old tablet servers to move the data away from them into the new set of tablet servers.

The commands below can be run from one of the old master nodes. You can first blacklist the six old tservers:

```sh
sudo yum -y install java

java -jar ~/master/java/yb-cli-0.8.0-SNAPSHOT.jar
yb> connect --masters node1:7100,node2:7100,node3:7100
Connected to database at node1:7100,node2:7100,node3:7100
yb> change_blacklist --isAdd true --servers node1:9100,node2:9100,node3:9100,node4:9100,node5:9100,node6:9100
```

Verify that the blacklist info looks similar to the output below:
```
java -jar ~/master/java/yb-cli-0.8.0-SNAPSHOT.jar
yb> get_universe_config
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

Next, wait for the data move to complete. You can check the percentage completion by running the command below.

```sh
java -jar ~/master/java/yb-cli-0.8.0-SNAPSHOT.jar
yb> get_load_move_completion
66.6
```

In the example above, the data move is `66.6%` done. Rerun this command periodically till the returned value reaches `100.0`.

{{< note title="Note" >}}
The time needed for this data move depends on the following:

- number of tablets/tables
- size of each of those tablets
- disk/ssd transfer speeds
- network bandwidth between the new nodes and the existing ones.
{{< /note >}}


## 5. Master quorum change
Now we move the master quorum from the old set of masters `node1`,`node2`,`node3` to the new set of masters `node7`,`node8`,`node9`. This is done by adding one new master followed by removing one old master sequentially, till all the old masters are removed. This can be run from one of the new masters.

`ADD_SERVER` step adds a new master and `REMOVE_SERVER` step removes an old master from the master quorum. After every step, it is recommended to check the Masters state on master UI home page (i.e., *http://`node7`:7000*).

{{< note title="Note" >}}
If there is any error log reported on the command line from the steps below, double check the master UI, as it might be a transitional error message and can be ignored.
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

Now we ensure that the master leader is one of the new master nodes.

```sh
$ export MASTERS=node7:7100,node8:7100,node9:7100
$ ~/master/bin/yb-admin -master_addresses $MASTERS list_all_masters
```
```
Master UUID         RPC Host/Port          State      Role
...                   node8:7100           ALIVE     FOLLOWER
...                   node9:7100           ALIVE     FOLLOWER
...                   node7:7100           ALIVE     LEADER
```
And confirm the same on *http://`node7`:7000/*, that the set of master IPs in the `Masters` list are only the three new ones.

On a new masters’ UI page, ensure that all the new tablet servers are reporting to the master leader and have the tablet load distributed. For example, *http://`node7`:7000/tablet-servers* should show the `Load` on six new tservers. The old tserver can be in `DEAD` status.


## 6. Update master addresses on tservers

The `tserver_master_addrs` parameter for all the new tserver processes needs to be set to the list of three new master IPs `node7:7100,node8:7100,node9:7100` for future use.

{{< note title="Tip" >}}
Updating master addresses is needed in case the yb-tserver process is restarted.
{{< /note >}}


## 7. Cleanup

The old nodes are not part of the universe any more and can be shutdown.
Once the old tserver processes are terminated, you can cleanup the blacklist from the master configuration using the command below.
```sh
java -jar ~/master/java/yb-cli-0.8.0-SNAPSHOT.jar
yb> connect --masters node7:7100,node8:7100,node9:7100
yb> change_blacklist --isAdd false --servers node1:9100,node2:9100,node3:9100,node4:9100,node5:9100,node6:9100
```

{{< note title="Tip" >}}
Cleaning up the blacklist server will help reuse the older IPs in case they get recycled.
{{< /note >}}

Ensure there are no `server_blacklist` entries returned by this java cli command:
```sh
yb> get_universe_config
```

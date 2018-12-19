---
title: Full Move Universe
linkTitle: Full Move Universe
description: Full Move Universe
aliases:
  - manage/full-move
menu:
  latest:
    identifier: manage-full-move
    parent: manage
    weight: 704
---

There might be situations where  want the data on the nodes in a universe needs to be moved to a new set of nodes. These include, but are not limited to:

- changing the IaaS instance/machine type.
- updating (rehydrate) the AMIs on a regular basis.
- moving to a different geo topology (ex., different set of regions).

YugaByte DB provides mechanisms that allow this form of data migration.

Below we provide the steps needed to migrate the data onto a new set of nodes in an online manner. For example, the data from a 6 node c3.large cluster (on say, AWS) can be moved to 6 nodes on c3.4xlarge machines, in an online fashion using these steps.

## 1. Setup New Machines

- Ensure the universe to be moved is not in any failure mode. That is, all yb-master and yb-tserver processes are running and able to talk to each other. This can be verified by checking the master UI (ex., *http://node1-ip:7000/tablet-servers*) which lists all the tablet servers. This is a sanity check to ensure that we do not inadvertently cause any further under replication on top of ongoing failures.

We will assume replication factor (RF) of 3. So the universe already has three master nodes, referred with IP’s node1-ip, node2-ip and node3-ip below. The other nodes’ (which host only tservers) ips are referred to as, node4-ip, node5-ip and node6-ip.


- Spin up a new set of VMs/machines (with the new AMI, for example). Ensure that the count of the new set of machines matches the node count in the existing universe. The requirement is that the ip addresses of the new machines are different for the old machines.

{{< note title="Note" >}}
If the universe is multi-az/multi-region, it is highly recommended to spin up the new set of nodes on the same set of AZs/regions with similar node count per AZ. This is referred to as the ‘placement’ of the universe.
{{< /note >}}


- Follow the steps for [System Configuration](../system-config/) and [YugaByte Software Installation](../install-software/) on each of the new machines.

Choose three of the new master nodes in the new set of machines, as the masters. We will be refer to them with IP’s node7-ip, node8-ip and node9-ip below. The other nodes (which will host only tservers) are say, node10-ip, node11-ip and node12-ip.

To read further about YugaByte DB process architecture, see [here](../../../../architecture/concepts/universe/). Now we start the server processes needed on each of the new nodes.


## 2. Start YugaByte DB processes

- Start the new master processes in *shell* mode.

Choose three nodes in the newly spawned set as master nodes, ideally mimicking the placement of masters in the current universe. The command below will bring up a master process, which is not yet part of the existing master quorum. This is in preparation for it to be added once the actual data migration step is completed.

```sh
export IP=<node-ip>
~/master/bin/yb-master --fs_data_dirs "/mnt/d0,/mnt/d1" \
                       --rpc_bind_addresses=$IP:7100 \
                       --webserver_interface=$IP \
                       >& /mnt/d0/yb-master.out &
```

This master is said to be in shell mode. This needs to be done on the three (as RF is 3) new chosen masters from the new set of nodes. We refer to these as node7-ip, node8-ip and node9-ip below.

{{< note title="Note" >}}
- The *--master_addresses* parameter should not be set for these new masters.
- The *--fs_data_dirs* parameter can be set to one or more directories as per the disk setup.
{{< /note >}}


- Start the new tablet server processes.

```sh
export IP=<node-ip>
export MASTERS=<node1-ip>:7100,<node2-ip>:7100,<node3-ip>:7100,<node7-ip>:7100,<node8-ip>:7100,<node9-ip>:7100
~/tserver/bin/yb-tserver --tserver_master_addrs $MASTERS \
                         --fs_data_dirs "/mnt/d0,/mnt/d1" \
                         --rpc_bind_addresses=$IP:9100 \
                         --cql_proxy_bind_address=$IP:9042 \
                         --redis_proxy_bind_address=$IP:6379 \
                         --webserver_interface=$IP \
                         >& /mnt/d0/yb-tserver.out &
```

This needs to be repeated on all the new set of nodes, which can be greater than or equal to the replication factor of the universe. In the example of six node universe, this has to be done on all six new nodes.

{{< note title="Note" >}}
The *tserver_master_addrs* parameter includes the new master ips as well, so that they can keep heart-beating/reporting to the new master, once the old masters are removed from quorum below (step 7). Also, during that master quorum change step, the master leader will update each new tserver with the latest master list.
{{< /note >}}


## 3. Perform Data Move
- Mark all the old tablet servers as blacklisted.
This helps move the data from those tablet servers into the new set of tablet servers. This is achieved by the [load balancing mechanism](https://docs.yugabyte.com/latest/architecture/concepts/universe/#leader-balancing) running periodically on the master leader. The java cli has mechanism to blacklist the tservers and then check for completion of data move from those servers, as shown in the commands below. This can be done from one of the old master nodes.

```sh
sudo yum -y install java

java -jar ~/master/java/yb-cli-0.8.0-SNAPSHOT.jar
yb> connect --masters <node1-ip>:7100,<node2-ip>:7100,<node3-ip>:7100
Connected to database at node1-ip:7100,node2-ip:7100,node3-ip:7100
yb> change_blacklist --isAdd true --servers <node1-ip>:9100
yb> change_blacklist --isAdd true --servers <node2-ip>:9100
yb> change_blacklist --isAdd true --servers <node3-ip>:9100
yb> change_blacklist --isAdd true --servers <node4-ip>:9100
yb> change_blacklist --isAdd true --servers <node5-ip>:9100
yb> change_blacklist --isAdd true --servers <node6-ip>:9100
```

As shown, this is done for all the (six) old tablet servers.

The blacklist info should be similar to the output below from the same java cli:
```sh
java -jar ~/master/java/yb-cli-0.8.0-SNAPSHOT.jar
yb> get_universe_config
Config:
version: 5
server_blacklist {
  hosts {
    host: "node1-ip"
    port: 9100
  }
  hosts {
    host: "node2-ip"
    port: 9100
  }
  ...
  ...
  hosts {
    host: "node6-ip"
    port: 9100
  }
}
```

- Wait for the data move to complete.
This command shows the percentage of the original load (as in, the number of tablets) that has been moved from the old tablet servers. Perform the following command till the returned value reaches 100%.

```sh
java -jar ~/master/java/yb-cli-0.8.0-SNAPSHOT.jar
yb> get_load_move_completion
66.6
```

One can also track the num tablet load on the six old tablet servers using the master UI at *http://node1-ip:7000/tablet-servers* and wait till it becomes 0. So all the tablets are moved to the new tablet server nodes.

{{< note title="Note" >}}
The time needed for this data move depends on the number of tablets/tables, the size of each of those tablets, disk/ssd transfer speeds and the network bandwidth between the new nodes and the existing ones.
{{< /note >}}


## 4. Master Quorum Change
Now that the data is moved, we can move the masters which track the metadata. This section provides steps to move the active set of masters from node1-ip,node2-ip,node3 to node7-ip,node8-ip,node9-ip. This is done by adding one new master and removing one old master respectively, repeated as pairs, till all the old masters are removed. This can be run from one of the new masters.

After the first step, it is recommended to check Masters tab on the node7-ip master UI home page (i.e., *http://node7-ip:7000*).

{{< note title="Note" >}}
If there is any error reported from the steps below, double check the UI, as it might be a transitional error. For example, if the old master leader was removed, but yb-admin still pings it and prints error message such as master addresses cannot be empty. We will work on cleaning this up soon as well.
{{< /note >}}

```sh
export MASTERS=<node1-ip>:7100,<node2-ip>:7100,<node3-ip>:7100,<node7-ip>:7100,<node8-ip>:7100,<node9-ip>:7100
~/master/bin/yb-admin -master_addresses $MASTERS change_master_config ADD_SERVER node7-ip 7100
~/master/bin/yb-admin -master_addresses $MASTERS change_master_config REMOVE_SERVER node1-ip 7100
~/master/bin/yb-admin -master_addresses $MASTERS change_master_config ADD_SERVER node8-ip 7100
~/master/bin/yb-admin -master_addresses $MASTERS change_master_config REMOVE_SERVER node2-ip 7100
~/master/bin/yb-admin -master_addresses $MASTERS change_master_config ADD_SERVER node9-ip 7100
~/master/bin/yb-admin -master_addresses $MASTERS change_master_config REMOVE_SERVER node3-ip 7100
```

ADD_SERVER step is adding a new master and REMOVE_SERVER steps will remove an old master from the master quorum. These need to be done one at a time due to [RAFT](https://raft.github.io/) requirements to guarantee only one leader for the quorum at any time.


- Ensure a master leader is present in one of the new master nodes.

```sh
$> export MASTERS=<node7-ip>:7100,<node8-ip>:7100,<node9-ip>:7100
$> ~/master/bin/yb-admin -master_addresses $MASTERS list_all_masters
Master UUID         RPC Host/Port            State     Role
...                 <node8-ip>:7100           ALIVE     FOLLOWER
...                 <node9-ip>:7100           ALIVE     FOLLOWER
...                 <node7-ip>:7100           ALIVE     LEADER
```


## 5. Cleanup

The old nodes are not part of the universe any more and can be terminated/shutdown.
Only once the old tserver processes are terminated, one can cleanup the blacklist from the master configuration as well using java cli.za
This will help if another move is performed, for example, and that new set of nodes have the older ips) using java cli.
```sh
java -jar ~/master/java/yb-cli-0.8.0-SNAPSHOT.jar
yb> connect --masters <node7-ip>:7100,<node8-ip>:7100,<node9-ip>:7100
yb> change_blacklist --isAdd false --servers <node1-ip>:9100
yb> change_blacklist --isAdd false --servers <node2-ip>:9100
yb> change_blacklist --isAdd false --servers <node3-ip>:9100
yb> change_blacklist --isAdd false --servers <node4-ip>:9100
yb> change_blacklist --isAdd false --servers <node5-ip>:9100
yb> change_blacklist --isAdd false --servers <node6-ip>:9100
```

Using
```sh
yb> get_universe_config
```
ensure there are no *server_blacklist* entries.

{{< note title="Note" >}}
If the new yb-tserver processes need to be restarted, the *tserver_master_addrs* parameter needs to be set to the list of three new master ip’s only.
{{< /note >}}

## 6. Verify

On one of the new masters’ UI page, ensure that the new tablet servers are reporting to the master quorum and have the tablet load distributed.
For example, *http://node7-ip:7000/tablet-servers*.

And confirm that on *http://node7-ip:7000/*, the set of master ips on the `Masters` tab are the three new ones only.

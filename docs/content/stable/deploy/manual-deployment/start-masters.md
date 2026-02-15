---
title: Deploy YugabyteDB
headerTitle: 3. Deploy
linkTitle: 3. Deploy
description: How to manually start the YB-Masters Server service for your YugabyteDB database cluster.
aliases:
  - /stable/deploy/manual-deployment/start-tservers/
menu:
  stable:
    identifier: deploy-2-manual
    parent: deploy-manual-deployment
    weight: 613
type: docs
---

This section describes how to deploy YugabyteDB in a single region or data center in a multi-zone/multi-rack configuration.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../start-yugabyted/" class="nav-link">
      <img src="/icons/database.svg" alt="Server Icon">
      yugabyted
    </a>
  </li>
  <li >
    <a href="../start-masters/" class="nav-link active">
      <i class="icon-shell"></i>
      Manual
    </a>
  </li>
</ul>

You can use the yb-tserver and yb-master binaries to manually start and configure the servers. For simplified deployment and management, use the [yugabyted](../../../reference/configuration/yugabyted/) configuration utility.

## Example scenario

- Create a six-node cluster with replication factor of 3.
  - [YB-Master](../../../architecture/yb-master/) server should run on only three nodes, the [YB-TServer](../../../architecture/yb-tserver/) server should run on all six nodes.
  - Assume the three YB-Master private IP addresses are `172.151.17.130`, `172.151.17.220` and `172.151.17.140`.
  - Cloud will be `aws`, region will be `us-west`, and the three AZs will be `us-west-2a`, `us-west-2b`, and `us-west-2c`. Two nodes will be placed in each AZ in such a way that one replica for each tablet (aka shard) gets placed in any one node for each AZ.
- Multiple data drives mounted on `/home/centos/disk1`, `/home/centos/disk2`.

Note that single zone configuration is a special case of multi-zone where all placement-related flags are set to the same value across every node.

For instructions on running a single cluster across multiple data centers or 2 clusters in 2 data centers, refer to [Multi-DC deployments](../../../deploy/multi-dc/).

## Configure YugabyteDB

To configure YugabyteDB, run the following shell script:

```sh
./bin/post_install.sh
```

## YB-Master servers

### Run YB-Master servers with command line flags

The number of nodes in a cluster running YB-Masters must equal the replication factor.

Run the yb-master server on each of the three nodes as follows.

```sh
$ ./bin/yb-master \
  --master_addresses 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
  --rpc_bind_addresses 172.151.17.130:7100 \
  --fs_data_dirs "/home/centos/disk1,/home/centos/disk2" \
  --placement_cloud aws \
  --placement_region us-west \
  --placement_zone us-west-2a \
  >& /home/centos/disk1/yb-master.out &
```

The number of comma-separated addresses in `--master_addresses` should equal the replication factor.

You can specify multiple directories using the [`--fs_data_dirs`](../../../reference/configuration/yb-master/#fs-data-dirs) flag. Replace the [`--rpc_bind_addresses`](../../../reference/configuration/yb-master/#rpc-bind-addresses) value with the private IP address of the host, and set the `placement_cloud`, `placement_region`, and `placement_zone` values appropriately. For single zone deployment, use the same value for the `placement_zone` flag.

{{<tags/feature/ea idea="1807">}} Highly accurate clocks can be configured by specifying `--time_source=clockbound`. Requires [system configuration](../system-config#set-up-time-synchronization).

For the full list of configuration flags, see the [YB-Master reference](../../../reference/configuration/yb-master/).

### Run YB-Master servers with configuration file

Alternatively, you can also create a `master.conf` file with the following flags and then run yb-master with the [`--flagfile`](../../../reference/configuration/yb-master/#flagfile) option as shown below. For each YB-Master server, replace the [`--rpc-bind-addresses`](../../../reference/configuration/yb-master/#rpc-bind-addresses) configuration flag with the private IP address of the YB-Master server.

```sh
--master_addresses=172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100
--rpc_bind_addresses=172.151.17.130:7100
--fs_data_dirs=/home/centos/disk1,/home/centos/disk2
--placement_cloud=aws
--placement_region=us-west
--placement_zone=us-west-2a
```

```sh
$ ./bin/yb-master --flagfile master.conf >& /home/centos/disk1/yb-master.out &
```

### Verify Master health

Make sure all the three YB-Masters are now working as expected by inspecting the INFO log. The default logs directory is always inside the first directory specified in the [`--fs_data_dirs`](../../../reference/configuration/yb-master/#fs-data-dirs) flag.

```sh
$ cat /home/centos/disk1/yb-data/master/logs/yb-master.INFO
```

You can see that the three YB-Masters were able to discover each other and were also able to elect a Raft leader among themselves (the remaining two act as Raft followers).

For the masters that become followers, you will see the following line in the log.

```output
I0912 16:11:07.419591  8030 sys_catalog.cc:332] T 00000000000000000000000000000000 P bc42e1c52ffe4419896a816af48226bc [sys.catalog]: This master's current role is: FOLLOWER
```

For the master that becomes the leader, you will see the following line in the log.

```output
I0912 16:11:06.899287 27220 raft_consensus.cc:738] T 00000000000000000000000000000000 P 21171528d28446c8ac0b1a3f489e8e4b [term 2 LEADER]: Becoming Leader. State: Replica: 21171528d28446c8ac0b1a3f489e8e4b, State: 1, Role: LEADER
```

{{< tip title="Tip" >}}

Remember to add the command with which you launched yb-master to a cron to restart it if it goes down.

{{< /tip >}}

## YB-TServer servers

### Run YB-TServer with command line flags

The number of nodes in a cluster running YB-TServers must equal or exceed the replication factor for any table to be created successfully.

Run the yb-tserver server on each of the six nodes as follows.

```sh
$ ./bin/yb-tserver \
  --tserver_master_addrs 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
  --rpc_bind_addresses 172.151.17.130:9100 \
  --enable_ysql \
  --pgsql_proxy_bind_address 172.151.17.130:5433 \
  --cql_proxy_bind_address 172.151.17.130:9042 \
  --fs_data_dirs "/home/centos/disk1,/home/centos/disk2" \
  --placement_cloud aws \
  --placement_region us-west \
  --placement_zone us-west-2a \
  >& /home/centos/disk1/yb-tserver.out &
```

Provide all of the master addresses using the [`--tserver_master_addrs`](../../../reference/configuration/yb-tserver/#tserver-master-addrs) flag. Replace the [`--rpc_bind_addresses`](../../../reference/configuration/yb-tserver/#rpc-bind-addresses) value with the private IP address of the host, and set the `placement_cloud`, `placement_region`, and `placement_zone` values appropriately. For single zone deployment, use the same value for the `--placement_zone` flag.

{{<tags/feature/ea idea="1807">}} Highly accurate clocks can be configured by specifying `--time_source=clockbound`. Requires [system configuration](../system-config#set-up-time-synchronization).

For the full list of configuration flags, see the [YB-TServer reference](../../../reference/configuration/yb-tserver/).

### Run YB-TServer with configuration file

Alternatively, you can also create a `tserver.conf` file with the following flags and then run the yb-tserver with the [`--flagfile`](../../../reference/configuration/yb-tserver/#flagfile) flag. For each YB-TServer server, replace the RPC bind address flags with the private IP address of the host running the YB-TServer server.

```sh
--tserver_master_addrs=172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100
--rpc_bind_addresses=172.151.17.130:9100
--enable_ysql
--pgsql_proxy_bind_address=172.151.17.130:5433
--cql_proxy_bind_address=172.151.17.130:9042
--fs_data_dirs=/home/centos/disk1,/home/centos/disk2
--placement_cloud=aws
--placement_region=us-west
--placement_zone=us-west-2a
```

```sh
$ ./bin/yb-tserver --flagfile tserver.conf >& /home/centos/disk1/yb-tserver.out &
```

### Set replica placement policy

{{< note title="Note" >}}

This step is required only for multi-AZ deployments and can be skipped for a single AZ deployment.

{{< /note >}}

The default replica placement policy when the cluster is first created is to treat all nodes as equal irrespective of the `--placement_*` configuration flags. However, for the current deployment, you want to explicitly place one replica of each tablet in each AZ. The following command sets replication factor of `3` across `us-west-2a`, `us-west-2b`, and `us-west-2c` leading to such a placement.

On any host running the yb-master, run the following command:

```sh
$ ./bin/yb-admin \
    --master_addresses 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
    modify_placement_info  \
    aws.us-west.us-west-2a,aws.us-west.us-west-2b,aws.us-west.us-west-2c 3
```

Verify by running the following:

```sh
$ curl -s http://<any-master-ip>:7000/cluster-config
```

Confirm that the output looks similar to the following, with `min_num_replicas` set to 1 for each AZ:

```output.json
replication_info {
  live_replicas {
    num_replicas: 3
    placement_blocks {
      cloud_info {
        placement_cloud: "aws"
        placement_region: "us-west"
        placement_zone: "us-west-2a"
      }
      min_num_replicas: 1
    }
    placement_blocks {
      cloud_info {
        placement_cloud: "aws"
        placement_region: "us-west"
        placement_zone: "us-west-2b"
      }
      min_num_replicas: 1
    }
    placement_blocks {
      cloud_info {
        placement_cloud: "aws"
        placement_region: "us-west"
        placement_zone: "us-west-2b"
      }
      min_num_replicas: 1
    }
  }
}
```

### Verify TServer health

Make sure all YB-TServer servers are working as expected by inspecting the INFO log. The default logs directory is always inside the first directory specified in the [`--fs_data_dirs`](../../../reference/configuration/yb-tserver/#fs-data-dirs) flag.

You can do this as follows:

```sh
$ cat /home/centos/disk1/yb-data/tserver/logs/yb-tserver.INFO
```

In each of the YB-TServer logs, you should see log messages similar to the following:

```output
I0912 16:27:18.296516  8168 heartbeater.cc:305] Connected to a leader master server at 172.151.17.140:7100
I0912 16:27:18.296794  8168 heartbeater.cc:368] Registering TS with master...
I0912 16:27:18.297732  8168 heartbeater.cc:374] Sending a full tablet report to master...
I0912 16:27:18.298435  8142 client-internal.cc:1112] Reinitialize master addresses from file: ../tserver.conf
I0912 16:27:18.298691  8142 client-internal.cc:1123] New master addresses: 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100
I0912 16:27:18.311367  8142 webserver.cc:156] Starting webserver on 0.0.0.0:12000
I0912 16:27:18.311408  8142 webserver.cc:161] Document root: /home/centos/yugabyte/www
I0912 16:27:18.311574  8142 webserver.cc:248] Webserver started. Bound to: http://0.0.0.0:12000/
I0912 16:27:18.311748  8142 rpc_server.cc:158] RPC server started. Bound to: 0.0.0.0:9042
I0912 16:27:18.311828  8142 tablet_server_main.cc:128] CQL server successfully started
```

In the current YB-Master leader log, you should see log messages similar to the following:

```output
I0912 22:26:32.832296  3162 ts_manager.cc:97] Registered new tablet server { permanent_uuid: "766ec935738f4ae89e5ff3ae26c66651" instance_seqno: 1505255192814357 } with Master
I0912 22:26:39.111896  3162 ts_manager.cc:97] Registered new tablet server { permanent_uuid: "9de074ac78a0440c8fb6899e0219466f" instance_seqno: 1505255199069498 } with Master
I0912 22:26:41.055996  3162 ts_manager.cc:97] Registered new tablet server { permanent_uuid: "60042249ad9e45b5a5d90f10fc2320dc" instance_seqno: 1505255201010923 } with Master
```

{{< tip title="Tip" >}}

Remember to add the command you used to start the YB-TServer to a `cron` job to restart it if it goes down.

{{< /tip >}}

## Grow the cluster

To grow the cluster, add additional YB-TServer nodes just as you do when creating the cluster.

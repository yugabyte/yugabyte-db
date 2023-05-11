---
title: Deploy to three or more data centers
headerTitle: Three+ data center (3DC)
linkTitle: Three+ data center (3DC)
description: Deploy YugabyteDB clusters to three or more data centers.
headContent: Deploy YugabyteDB clusters across regions
menu:
  stable:
    parent: multi-dc
    identifier: 3dc-deployment
    weight: 632
type: docs
---

{{< tip title="Recommended Reading" >}}

[9 Techniques to Build Cloud-Native, Geo-Distributed SQL Apps with Low Latency](https://www.yugabyte.com/blog/9-techniques-to-build-cloud-native-geo-distributed-sql-apps-with-low-latency/) highlights the various multi-DC deployment strategies (including 3DC deployments) for a distributed SQL database like YugabyteDB.

{{< /tip >}}

Three data center deployments of YugabyteDB are essentially a natural extension of the three availability zone (AZ) deployments documented in the [Manual deployment](../../manual-deployment/) section. Equal number of nodes are placed in each of the three data centers. Inside a single data center, a multi-AZ deployment is recommended to ensure resilience against zone failures. This approach works fine for any odd number of AZs or data centers. Given YugabyteDB's distributed consensus-based replication, which requires majority quorum for continuous availability of write requests, deploying a single cluster across an even number of AZs or data centers is not recommended.

## Example scenario

- Create a three-node cluster with replication factor of `3`.

  - Cloud is `aws` and the three regions/AZs are `us-west`/`us-west-2a`, `us-east-1`/`us-east-1a`, `ap-northeast-1`/`ap-northeast-1a`. One node is placed in each region/AZ such that one replica for each tablet is also placed in each region/AZ.
  - Private IP addresses of the 3 nodes are `172.151.17.130`, `172.151.17.220`, and `172.151.17.140`.

- Multiple data drives are mounted on `/home/centos/disk1`, `/home/centos/disk2`.

### Prerequisites

Follow the [Checklist](../../../deploy/checklist/) to ensure you have prepared the nodes for installing YugabyteDB.

## Deploy the cluster

Execute the following steps on each of the instances.

### Install software

Follow the [installation instructions](../../../deploy/manual-deployment/install-software) to install YugabyteDB on each of the nodes.

### Start YB-Masters

Run the [yb-master](../../../reference/configuration/yb-master/) server on each of the nodes as follows:

```sh
$ ./bin/yb-master \
  --master_addresses 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
  --rpc_bind_addresses 172.151.17.130 \
  --fs_data_dirs "/home/centos/disk1,/home/centos/disk2" \
  --placement_cloud aws \
  --placement_region us-west \
  --placement_zone us-west-2a \
  --leader_failure_max_missed_heartbeat_periods 10 \
  >& /home/centos/disk1/yb-master.out &
```

Note how multiple directories can be provided to the `--fs_data_dirs` flag. Replace the [`--rpc_bind_addresses`](../../../reference/configuration/yb-master/#rpc-bind-addresses) value with the private IP address of the host as well as the set the `--placement_cloud`,`--placement_region`, and `--placement_zone` values appropriately.

Notice how the [`--leader_failure_max_missed_heartbeat_periods`](../../../reference/configuration/yb-master/#leader-failure-max-missed-heartbeat-periods) flag is set to `10`. This flag specifies the maximum heartbeat periods that the leader can fail to heartbeat before the leader is considered to be failed. Because the data is geo-replicated across data centers, RPC latencies are expected to be higher. Use this flag to increase the failure detection interval in higher RPC latency deployments.

The total failure timeout is now 5 seconds, and is calculated by multiplying [`--raft_heartbeat_interval_ms`](../../../reference/configuration/yb-master/#raft-heartbeat-interval-ms) (default of 500ms) by [`--leader_failure_max_missed_heartbeat_periods`](../../../reference/configuration/yb-master/#leader-failure-max-missed-heartbeat-periods) (current value of `10`).

For the full list of configuration flags, see the [YB-Master reference](../../../reference/configuration/yb-master/).

### Start YB-TServers

Run the [yb-tserver](../../../reference/configuration/yb-tserver/) server on each node as follows:

```sh
$ ./bin/yb-tserver \
  --tserver_master_addrs 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
  --rpc_bind_addresses 172.151.17.130 \
  --enable_ysql \
  --pgsql_proxy_bind_address 172.151.17.130:5433 \
  --cql_proxy_bind_address 172.151.17.130:9042 \
  --fs_data_dirs "/home/centos/disk1,/home/centos/disk2" \
  --placement_cloud aws \
  --placement_region us-west \
  --placement_zone us-west-2a \
  --leader_failure_max_missed_heartbeat_periods 10 \
  >& /home/centos/disk1/yb-tserver.out &
```

Note that all of the master addresses have to be provided using the [`--tserver_master_addrs`](../../../reference/configuration/yb-master/#tserver-master-addrs) flag. Replace the [`--rpc_bind_addresses`](../../../reference/configuration/yb-tserver/#rpc-bind-addresses) value with the private IP address of the host as well as the set the `placement_cloud`,`placement_region`, and `placement_zone` values appropriately.

As with the YB-Masters, set the [`--leader_failure_max_missed_heartbeat_periods`](../../../reference/configuration/yb-tserver/#leader-failure-max-missed-heartbeat-periods) flag to `10` to account for higher RPC latencies.

For the full list of configuration flags, see the [YB-TServer reference](../../../reference/configuration/yb-tserver/).

### Set replica placement policy

The default replica placement policy when the cluster is first created is to treat all nodes as equal irrespective of the `--placement_*` configuration flags. However, for the current deployment, you want to explicitly place one replica of each tablet in each region/AZ. The following command sets replication factor of `3` across `us-west-2`/`us-west-2a`, `us-east-1`/`us-east-1a`, `ap-northeast-1`/`ap-northeast-1a` leading to such a placement.

On any host running the yb-master, run the following command:

```sh
$ ./bin/yb-admin \
    --master_addresses 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
    modify_placement_info  \
    aws.us-west.us-west-2a,aws.us-east-1.us-east-1a,aws.ap-northeast-1.ap-northeast-1a 3
```

Verify by running the following:

```sh
$ curl -s http://<any-master-ip>:7000/cluster-config
```

Confirm that the output looks similar to the following with [`--min_num_replicas`](../../../reference/configuration/yb-tserver/#min-num-replicas) set to `1` for each AZ:

```json
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
        placement_region: "us-east-1"
        placement_zone: "us-east-1a"
      }
      min_num_replicas: 1
    }
    placement_blocks {
      cloud_info {
        placement_cloud: "aws"
        placement_region: "ap-northeast-1"
        placement_zone: "ap-northeast-1a"
      }
      min_num_replicas: 1
    }
  }
}
```

### Set a preferred location

One additional option to consider is to set a preferred location for all the tablet leaders using the [yb-admin set_preferred_zones](../../../admin/yb-admin#set-preferred-zones) command.

For multi-row or multi-table transactional operations, colocating the leaders in a single zone or region can help reduce the number of cross-region network hops involved in executing a transaction and, as a result, improve performance.

The following command sets the preferred zone to `aws.us-west.us-west-2a`:

```sh
$ ./bin/yb-admin \
    --master_addresses 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
    set_preferred_zones  \
    aws.us-west.us-west-2a
```

Looking again at the cluster configuration, you should see `affinitized_leaders` added:

```json
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
        placement_region: "us-east-1"
        placement_zone: "us-east-1a"
      }
      min_num_replicas: 1
    }
    placement_blocks {
      cloud_info {
        placement_cloud: "aws"
        placement_region: "ap-northeast-1"
        placement_zone: "ap-northeast-1a"
      }
      min_num_replicas: 1
    }
    affinitized_leaders {
      placement_cloud: "aws"
      placement_region: "us-west"
      placement_zone: "us-west-2a"
    }
  }
}
```

## Verify the deployment

Use the [`ysqlsh`](../../../admin/ysqlsh/) (for YSQL API) or [`ycqlsh`](../../../admin/cqlsh/) (for YCQL API) shells to test connectivity to the cluster.

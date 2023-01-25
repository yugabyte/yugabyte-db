---
title: Synchronous replication (3+ regions)
headerTitle: Synchronous replication (3+ regions)
linkTitle: Synchronous (3+ regions)
description: Global data distributed using synchronous replication across regions.
headcontent: Distribute data across regions
aliases:
  - /preview/explore/global-distribution-linux/
  - /preview/explore/global-distribution/macos
  - /preview/explore/global-distribution/linux
menu:
  preview:
    identifier: explore-multi-region-deployments-sync-replication-1-ysql
    parent: explore-multi-region-deployments
    weight: 710
type: docs
---

YugabyteDB can be deployed in a globally distributed manner to serve application queries from the region closest to end users with low latencies, as well as to survive any outages to ensure high availability.

In a synchronized multi-region cluster, a minimum of 3 nodes are spread across 3 regions with a replication factor (RF) of 3.

This deployment provides the following advantages:

- Resilience. Putting cluster nodes in different regions provides a higher degree of failure independence. In the event of a region failure, the database cluster continues to serve data requests from the remaining regions. YugabyteDB automatically performs a failover to the nodes in the other two regions, and the tablets being failed over are evenly distributed across the two remaining regions.

- Consistency. All writes are synchronously replicated. Transactions are globally consistent.

## Tuning latencies

Latency in a multi-region cluster depends on the distance/network packet transfer times between the nodes of the cluster and between the cluster and the client. Because the tablet leader replicates write operations across a majority of tablet peers before sending a response to the client, all writes involve cross-region communication between tablet peers.

### Preferred region

If application reads are known to be originating dominantly from a single region, you can configure the cluster to have the shard leaders pinned to that single region. When you designate a preferred region, all the shard leaders are placed in that region, and it handles all read and write requests from clients. Non-preferred regions are used only for hosting shard follower replicas. Note that cross-region latencies are unavoidable in the write path given the need to ensure region-level automatic failover and repair.

### Follower reads

YugabyteDB offers tunable global reads that allow read requests to trade off some consistency for lower read latency. By default, read requests in a YugabyteDB cluster are handled by the leader of the Raft group associated with the target tablet to ensure strong consistency. In situations where you are willing to sacrifice some consistency in favor of lower latency, you can choose to read from a tablet follower that is closer to the client rather than from the leader. YugabyteDB also allows you to specify the maximum staleness of data when reading from tablet followers.

For more information on follower reads, refer to the [Follower reads](../../ysql-language-features/going-beyond-sql/follower-reads-ysql/) example.

## Create a synchronized multi-region cluster

This example simulates AWS regions on a local machine.

If you have a previously running local cluster, destroy it.

{{< note title="Setup for POCs" >}}

The steps can also be used for deploying clusters in any public cloud, private data center, or in separate VMs. The only differences are as follows:

- You don't need to specify the `--advertise_address` or `--base_dir` flags.
- You don't need to configure loopback addresses.
- Replace the IP addresses in the commands with the corresponding IP addresses of your nodes.

{{< /note >}}

Start a 3-node cluster with a replication factor (RF) of `3`, and each replica placed in different AWS regions (`us-west-2`, `us-east-1`, `ap-northeast-1`) as follows:

1. Create a single node cluster as follows:

    ```sh
    ./bin/yugabyted start \
                    --advertise_address 127.0.0.1 \
                    --base_dir=/tmp/ybd1 \
                    --cloud_location aws.us-west-2.us-west-2a \
                    --fault_tolerance region
    ```

1. When creating a local cluster on MacOS, the additional nodes need loopback addresses configured:

    ```sh
    sudo ifconfig lo0 alias 127.0.0.2
    sudo ifconfig lo0 alias 127.0.0.3
    ```

1. Join two more nodes with the previous node. By default, [yugabyted](../../../reference/configuration/yugabyted/) creates a cluster with a replication factor of `3` when the third node is added.

    ```sh
    ./bin/yugabyted start \
                    --advertise_address 127.0.0.2 \
                    --base_dir=/tmp/ybd2 \
                    --cloud_location aws.us-east-1.us-east-1a \
                    --fault_tolerance region \
                    --join 127.0.0.1
    ```

    ```sh
    ./bin/yugabyted start \
                    --advertise_address 127.0.0.3 \
                    --base_dir=/tmp/ybd3 \
                    --cloud_location aws.ap-northeast-1.ap-northeast-1a \
                    --fault_tolerance region \
                    --join 127.0.0.1
    ```

1. After starting the yugabyted processes on all the nodes, configure the data placement constraint of the cluster as follows:

    ```sh
    ./bin/yugabyted configure data_placement --base_dir=/tmp/ybd1 --fault_tolerance=region
    ```

The [configure](../../../reference/configuration/yugabyted/#configure) command determines the data placement constraint based on the `--cloud_location` of each node in the cluster. If three or more regions are available in the cluster, `configure` configures the cluster to survive at least one region failure. Otherwise, it outputs a warning message. The command can be executed on any node where you already started YugabyteDB.

## Review the deployment

In this deployment, the YB-Masters are each placed in a separate region to allow them to survive the loss of a region. You can view the tablet servers on the [tablet servers page](http://localhost:7000/tablet-servers), as per the following illustration:

![Multi-zone cluster YB-TServers](/images/ce/online-reconfig-multi-zone-tservers.png)

## Start a workload

Follow the [setup instructions](../../#set-up-yb-workload-simulator) to connect the YB Workload Simulator application, and run a read-write workload. To verify that the application is running correctly, navigate to the application UI at <http://localhost:8080/> to view the cluster network diagram and Latency and Throughput charts for the running workload.

You should now see some read and write load on the [tablet servers page](http://localhost:7000/tablet-servers), as per the following illustration:

![Multi-zone cluster load](/images/ce/online-reconfig-multi-zone-load.png)

The load is distributed evenly across the regions.

## Set a preferred region

For best performance as well as lower data transfer costs, you want to minimize transfers between providers, and between provider regions. You do this by locating your cluster as close to your applications as possible:

- Use the same cloud provider as your application.
- Locate your cluster in the same region as your application.

To further improve performance, you can designate the region closest to your client as the preferred region for all the tablet leaders. This places all the leaders in the preferred region, and as a result, the preferred region handles all read and write requests from clients. For multi-row or multi-table transactional operations, colocating the leaders in a single zone or region can help reduce the number of cross-region network hops involved in executing a transaction.

The following command sets the preferred zone to `aws.us-west-2.us-west-2a`:

```sh
$ ./bin/yb-admin \
    --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
    set_preferred_zones  \
    aws.us-west-2.us-west-2a
```

You should see the read and write load on the [tablet servers page](http://localhost:7000/tablet-servers) move to the preferred region, as per the following illustration:

![Multi-zone cluster load](/images/ce/online-reconfig-multi-zone-pref-load.png)

When complete, the load is handled exclusively by the preferred region.

## Clean up

Optionally, you can shutdown the local cluster as follows:

```sh
./bin/yugabyted destroy --base_dir=/tmp/ybd1
./bin/yugabyted destroy --base_dir=/tmp/ybd2
./bin/yugabyted destroy --base_dir=/tmp/ybd3
```

## Learn more

[Replication](../../../architecture/docdb-replication/replication/)

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
  - /preview/explore/multi-region-deployments/synchronous-replication-ycql/
menu:
  preview:
    identifier: explore-multi-region-deployments-sync-replication-1-ysql
    parent: explore-multi-region-deployments
    weight: 710
type: docs
---

For protection in the event of the failure of an entire cloud region, you can deploy YugabyteDB across multiple regions with a synchronously replicated multi-region cluster. In a synchronized multi-region cluster, a minimum of 3 nodes are [replicated](../../../architecture/docdb-replication/replication/) across 3 regions with a replication factor (RF) of 3. In the event of a region failure, the database cluster continues to serve data requests from the remaining regions. YugabyteDB automatically performs a failover to the nodes in the other two regions, and the tablets being failed over are evenly distributed across the two remaining regions.

This deployment provides the following advantages:

- Resilience. Putting cluster nodes in different regions provides a higher degree of failure independence.
- Consistency. All writes are synchronously replicated. Transactions are globally consistent.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../synchronous-replication-ysql/" class="nav-link active">
      <img src="/icons/database.svg" alt="Server Icon">
      Local
    </a>
  </li>
  <li>
    <a href="../synchronous-replication-cloud/" class="nav-link">
      <img src="/icons/cloud.svg" alt="Cloud Icon">
      YugabyteDB Managed
    </a>
  </li>
</ul>

This example simulates AWS regions on a local machine.

If you have a previously running local cluster, [destroy it](#clean-up).

{{< note title="Setup for POCs" >}}

The steps can also be used for deploying clusters in any public cloud, private data center, or in separate virtual machines. The only differences are as follows:

- You don't need to specify the `--advertise_address` or `--base_dir` flags.
- You don't need to configure loopback addresses.
- Replace the IP addresses in the commands with the corresponding IP addresses of your nodes.

{{< /note >}}

## Create a synchronized multi-region cluster

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

    **If you are running a version prior to 2.17.1.0**, run the following command instead:

    ```sh
    ./bin/yugabyted configure --base_dir=/tmp/ybd1 --fault_tolerance=region
    ```

The [configure](../../../reference/configuration/yugabyted/#configure) command determines the data placement constraint based on the `--cloud_location` of each node in the cluster. If three or more regions are available in the cluster, `configure` configures the cluster to survive at least one region failure. Otherwise, it outputs a warning message. The command can be executed on any node where you already started YugabyteDB.

## Review the deployment

In this deployment, the YB-Masters are each placed in a separate region to allow them to survive the loss of a region. You can view the tablet servers on the [tablet servers page](http://localhost:7000/tablet-servers), as per the following illustration:

![Multi-region cluster YB-TServers](/images/ce/online-reconfig-multi-zone-tservers.png)

## Start a workload

Follow the [setup instructions](../../#set-up-yb-workload-simulator) to install the YB Workload Simulator application.

### Configure the smart driver

The YugabyteDB JDBC Smart Driver performs uniform load balancing by default, meaning it uniformly distributes application connections across all the nodes in the cluster. However, in a multi-region cluster, it's more efficient to target regions closest to your application.

You can configure the smart driver with [topology load balancing](../../../drivers-orms/smart-drivers/#topology-aware-connection-load-balancing) to limit connections to the closest region.

To turn on topology load balancing, start the application as usual, adding the following flag:

```sh
-Dspring.datasource.hikari.data-source-properties.topologyKeys=<cloud.region.zone>
```

Where `cloud.region.zone` is the location of the zone where your application is hosted.

If you are running the application locally, set the value to the cloud location of the node you are connecting to. For example, if you are connecting to 127.0.0.1, set the value to `aws.us-west-2.us-west-2a` as follows:

```sh
java -jar \
    -Dnode=127.0.0.1 \
    -Dspring.datasource.hikari.data-source-properties.topologyKeys=aws.us-west-2.us-west-2a \
    ./yb-workload-sim-0.0.3.jar
```

After you are connected, [start a workload](../../#start-a-read-and-write-workload).

## View cluster activity

To verify that the application is running correctly, navigate to the application UI at <http://localhost:8080/> to view the cluster network diagram and Latency and Throughput charts for the running workload.

You should now see some read and write load on the [tablet servers page](http://localhost:7000/tablet-servers), as per the following illustration:

![Multi-region cluster load](/images/ce/online-reconfig-multi-zone-load.png)

## Tuning latencies

Latency in a multi-region cluster depends on the distance/network packet transfer times between the nodes of the cluster and between the cluster and the client. Because the [tablet leader](../../../architecture/core-functions/write-path/#preparation-of-the-operation-for-replication-by-tablet-leader) replicates write operations across a majority of tablet peers before sending a response to the client, all writes involve cross-region communication between tablet peers.

For best performance as well as lower data transfer costs, you want to minimize transfers between providers, and between provider regions. You do this by locating your cluster as close to your applications as possible:

- Use the same cloud provider as your application.
- Locate your cluster in the same region as your application.
- Peer your cluster with the VPC hosting your application.

### Follower reads

YugabyteDB offers tunable global reads that allow read requests to trade off some consistency for lower read latency. By default, read requests in a YugabyteDB cluster are handled by the leader of the Raft group associated with the target tablet to ensure strong consistency. In situations where you are willing to sacrifice some consistency in favor of lower latency, you can choose to read from a tablet follower that is closer to the client rather than from the leader. YugabyteDB also allows you to specify the maximum staleness of data when reading from tablet followers.

For more information on follower reads, refer to the [Follower reads](../../ysql-language-features/going-beyond-sql/follower-reads-ysql/) example.

### Preferred region

If application reads and writes are known to be originating primarily from a single region, you can designate a preferred region, which pins the tablet leaders to that single region. As a result, the preferred region handles all read and write requests from clients. Non-preferred regions are used only for hosting tablet follower replicas.

For multi-row or multi-table transactional operations, colocating the leaders in a single zone or region can help reduce the number of cross-region network hops involved in executing a transaction.

The following command sets the preferred zone to `aws.us-west-2.us-west-2a`:

```sh
$ ./bin/yb-admin \
    --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
    set_preferred_zones  \
    aws.us-west-2.us-west-2a
```

You should see the read and write load on the [tablet servers page](http://localhost:7000/tablet-servers) move to the preferred region, as per the following illustration:

![Multi-region cluster preferred load](/images/ce/online-reconfig-multi-zone-pref-load.png)

When complete, the load is handled exclusively by the preferred region.

## Clean up

To destroy the local cluster, do the following:

```sh
./bin/yugabyted destroy --base_dir=/tmp/ybd1
./bin/yugabyted destroy --base_dir=/tmp/ybd2
./bin/yugabyted destroy --base_dir=/tmp/ybd3
```

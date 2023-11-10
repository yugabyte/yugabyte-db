---
title: High availability during failures in YugabyteDB Anywhere
headerTitle: High availability during node and zone failures
linkTitle: HA during failures
description: Simulate fault tolerance and resilience in a YugabyteDB Anywhere universe.
headcontent: Keep serving requests through node, zone, and region failures
menu:
  stable:
    identifier: node-failure-2-macos
    parent: fault-tolerance
    weight: 10
type: docs
---

The following example demonstrates how YugabyteDB can continue to perform reads and writes even in case of node failures. In this scenario, you create a universe with a replication factor (RF) of 3, which allows a [fault tolerance](../../../architecture/docdb-replication/replication/#fault-tolerance) of 1. This means the universe remains available for both reads and writes even if a fault domain fails. However, if another were to fail (bringing the number of failures to two), writes would become unavailable on the universe to preserve data consistency.

The examples are based on the YB Workload Simulator application, which uses the YugabyteDB JDBC [Smart Driver](../../../drivers-orms/smart-drivers/) configured with connection load balancing. The driver automatically balances application connections across the nodes in a universe and rebalances connections when a node fails.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../macos/" class="nav-link">
      <img src="/icons/database.svg" alt="Server Icon">
      Local
    </a>
  </li>
  <li>
    <a href="../macos-yba/" class="nav-link active">
      <img src="/icons/server.svg" alt="Server Icon">
      YugabyteDB Anywhere
    </a>
  </li>
</ul>

## Set up a universe

Follow the [setup instructions](../../#set-up-yugabytedb-universe) to start a single region three-node universe in YugabyteDB Anywhere, connect the [YB Workload Simulator](../../#set-up-yb-workload-simulator) application, and run a read-write workload. To verify that the application is running correctly, navigate to the application UI at <http://localhost:8080/> to view the universe network diagram, as well as latency and throughput charts for the running workload.

## Observe even load across all nodes

You can use YugabyteDB Anywhere to view per-node statistics for the universe, as follows:

1. Navigate to **Universes** and select your universe.

1. Select **Nodes** to view the total read and write IOPS per node and other statistics, as shown in the following illustration:

    ![Read and write IOPS with 3 nodes](/images/ce/transactions_anywhere_observe1.png)

    Notice that both the reads and the writes are approximately the same across all nodes, indicating uniform load.

1. Select **Metrics** to view charts such as YSQL operations per second and latency, as shown in the following illustration:

    ![Performance charts for 3 nodes](/images/ce/transactions_anywhere_chart.png)

1. Navigate to the [YB Workload Simulator application UI](http://127.0.0.1:8080/) to view the latency and throughput on the universe while the workload is running, as per the following illustration:

    ![Latency and throughput with 3 nodes](/images/ce/simulation-graph-cloud.png)

## Simulate a node failure

You can stop one of the nodes to simulate the loss of a zone, as follows:

1. Navigate to **Universes** and select your universe.

1. Select **Nodes**, find the node to be removed, and then click its corresponding **Actions > Stop Processes**.

## Observe workload remains available

1. Verify the details by selecting **Nodes**. Expect to see that the load has been moved off the stopped node and redistributed to the remaining nodes, as shown in the following illustration:

    ![Read and write IOPS with one node stopped](/images/ce/stop-node-yba.png)

1. Navigate to **Metrics** to observe a slight spike and drop in the latency and YSQL Ops / Sec charts when the node is stopped, as shown in the following illustration:

    ![Performance metrics with a node dead](/images/ce/stop-node-chart-yba.png)

Alternatively, you can navigate to the [YB Workload Simulator application UI](http://127.0.0.1:8080/) to see the node being removed from the network diagram when it is stopped (it may take a few minutes to display the updated network diagram). Also notice a slight spike and drop in the latency and throughput, both of which resume immediately, as shown in the following illustration:

![Latency and throughput graph after dropping a node](/images/ce/fault-tolerance-latency-stoppednode.png)

With the loss of the node, which also represents the loss of an entire fault domain, the universe is now in an under-replicated state.

Despite the loss of an entire fault domain, there is no impact on the application because no data is lost; previously replicated data on the remaining nodes is used to serve application requests.

{{% explore-cleanup-local %}}

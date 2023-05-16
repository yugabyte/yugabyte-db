---
title: Scaling transactions in YugabyteDB Anywhere
headerTitle: Scaling concurrent transactions
linkTitle: Scaling concurrent transactions
description: Scaling concurrent transactions in YugabyteDB Anywhere.
headcontent: Horizontal scale-out and scale-in in YugabyteDB
menu:
  stable:
    name: Scaling transactions
    identifier: explore-transactions-scaling-transactions-2-ysql
    parent: explore-scalability
    weight: 210
type: docs
---

With YugabyteDB, you can add nodes to upscale your universe efficiently and reliably to achieve more read and write IOPS (input/output operations per second), without any downtime.

This document demonstrates how YugabyteDB can scale seamlessly while running a read-write workload. Using the [YB Workload Simulator application](https://github.com/YugabyteDB-Samples/yb-workload-simulator) against a three-node universe running in AWS, with a replication factor of 3, you add a node while the workload is running. Using the built-in metrics, you can observe how the universe scales out by verifying that the number of read and write IOPS are evenly distributed across all the nodes at all times.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../scaling-transactions/" class="nav-link">
      <img src="/icons/database.svg" alt="Server Icon">
      Local
    </a>
  </li>
  <li>
    <a href="../scaling-transactions-cloud/" class="nav-link">
      <img src="/icons/cloud.svg" alt="Cloud Icon">
      YugabyteDB Managed
    </a>
  </li>
  <li>
    <a href="../scaling-transactions-yba/" class="nav-link active">
      <img src="/icons/server.svg" alt="Server Icon">
      YugabyteDB Anywhere
    </a>
  </li>
</ul>

## Set up a universe

Follow the [setup instructions](../../#set-up-yugabytedb-universe) to start a three-node universe in YugabyteDB Anywhere, connect the [YB Workload Simulator](../../#set-up-yb-workload-simulator) application, and run a read-write workload. To verify that the application is running correctly, navigate to the application UI at <http://localhost:8080/> to view the universe network diagram, as well as Latency and Throughput charts for the running workload.

## Observe IOPS per node

You can use YugabyteDB Anywhere to view per-node statistics for the universe, as follows:

1. Navigate to **Universes** and select your universe.

1. Select **Nodes** to view the total read and write IOPS per node and other statistics, as shown in the following illustration:

    ![Read and write IOPS with 3 nodes](/images/ce/transactions_anywhere_observe1.png)

    Note that both the reads and the writes are approximately the same across all the nodes, indicating uniform load.

1. Select **Metrics** to view charts such as YSQL operations per second and latency, as shown in the following illustration:

    ![Performance charts for 3 nodes](/images/ce/transactions_anywhere_chart.png)

1. Navigate to the [YB Workload Simulator application UI](http://127.0.0.1:8080/) to view the latency and throughput on the universe while the workload is running, as per the following illustration:

    ![Latency and throughput with 3 nodes](/images/ce/simulation-graph-cloud.png)

## Add a node

You can add a node to the universe in YugabyteDB Anywhere as follows:

1. Navigate to **Universes**, select your universe, and click **Actions > Edit Universe**.

1. In the **Nodes** field, change the number to 4 to add a new node.

1. Click **Save**. Note that the scaling operation can take several minutes.

### Observe linear scale-out

Verify that the node has been added by selecting **Nodes**, as per the following illustration:

![Read and write IOPS with 4 nodes](/images/ce/add-node-anywhere.png)

Shortly, you should see the new node performing a comparable number of reads and writes as the other nodes. The tablets are also distributed evenly across all four nodes.

The universe automatically lets the client know to use the newly added node for serving queries. This scaling out of client queries is completely transparent to the application logic, allowing the application to scale linearly for both reads and writes.

Navigate to [Metrics](../../../yugabyte-platform/troubleshoot/universe-issues/#use-metrics) to observe a slight spike and drop in the latency and YSQL Ops / Sec charts when the node is added, and then both return to normal, as shown in the following illustration:

![Latency and throughput graph with 4 nodes](/images/ce/add-node-anywhere-chart.png)

Alternatively, you can navigate to the [YB Workload Simulator application UI](http://127.0.0.1:8080/) to see the new node being added to the network diagram. You can also notice a slight spike and drop in the latency and throughput when the node is added, and then both return to normal, as shown in the following illustration:

![Latency and throughput graph with 4 nodes](/images/ce/add-node-graph-cloud.png)

## Remove a node

You can remove a node from the universe as follows:

1. Navigate to **Universes** and select your universe.

1. Select **Nodes**, find the node to be removed, and then click its corresponding **Actions > Remove Node**.

### Observe linear scale-in

Verify that the details by selecting **Nodes**. The scale-in operation can take several minutes and expect to see that the load has been moved off the removed node and redistributed to the remaining nodes.

Navigate to **Metrics** to observe a slight spike and drop in the latency and YSQL Ops / Sec charts when the node is removed, and then both return to normal, as shown in the following illustration:

![Performance metrics with 4th node dead](/images/ce/stop-node-chart-anywhere.png)

Alternatively, you can navigate to the [YB Workload Simulator application UI](http://127.0.0.1:8080/) to see the node being removed from the network diagram when it is stopped. Also notice a slight spike and drop in the latency and throughput, both of which resume immediately, as shown in the following illustration:

![Latency and throughput graph after stopping node 4](/images/ce/stop-node-graph-cloud.png)

## Clean up

You can delete your universe by following instructions provided in [Delete a universe](../../../yugabyte-platform/manage-deployments/delete-universe/).

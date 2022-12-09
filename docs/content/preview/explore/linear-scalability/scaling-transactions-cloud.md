---
title: Scaling transactions
headerTitle: Scaling concurrent transactions
linkTitle: Scaling concurrent transactions
description: Scaling concurrent transactions in YugabyteDB Managed.
headcontent: Horizontal scale-out and scale-in in YugabyteDB
menu:
  preview:
    name: Scaling transactions
    identifier: explore-transactions-scaling-transactions-3-ysql
    parent: explore-scalability
    weight: 200
type: docs
---

With YugabyteDB, you can add nodes to upscale your cluster efficiently and reliably to achieve more read and write IOPS (input/output operations per second), without any downtime.

<<<<<<< HEAD
This tutorial shows how YugabyteDB can scale seamlessly while running a read-write workload. Using the [YB Workload Simulator application](https://github.com/yugabyte/yb-simulation-base-demo-app) against a single region three-node cluster on YugabyteDB Managed with a replication factor of 3, you add a node while the workload is running. Using the built-in metrics, you can observe how the cluster scales out by verifying that the number of read and write IOPS are evenly distributed across all the nodes at all times.

=======
>>>>>>> 77f0399675644ff4f53d7499de3df2ecfea93181
<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../scaling-transactions-cloud/" class="nav-link active">
              <img src="/icons/cloud-icon.svg" alt="Icon">
Use a cloud cluster
    </a>
  </li>
  <li>
    <a href="../scaling-transactions/" class="nav-link">
              <img src="/icons/server-iconsvg.svg" alt="Icon">
Use a local cluster
    </a>
  </li>
</ul>

This tutorial shows how YugabyteDB can scale seamlessly while running a read-write workload. The example uses the [YB Simulation Base Demo application](https://github.com/yugabyte/yb-simulation-base-demo-app) to run a read-write workload against a three-node cluster with a replication factor of 3. While the workload is running, you add and then remove a node from the cluster. Using the built-in metrics, you can observe how the cluster scales out and in by verifying that the number of read and write IOPS are evenly distributed across all the nodes at all times.

{{% explore-setup-multi-cloud %}}

Follow the setup instructions to create a three-node cluster in YugabyteDB Managed, connect the YB Simulation Base Demo application, and run a read-write workload. To verify that the application is running correctly, navigate to the application UI at <http://localhost:8080/> to view the cluster network diagram and Latency and Throughput charts for the running workload.

## Observe IOPS per node

To view a table of per-node statistics for the cluster, do the following:

1. On the **Clusters** page, select your cluster, and then select the **Nodes** tab.

    ![Read and write IOPS with 3 nodes](/images/ce/transactions_cloud_observe1.png)

    Both reads and writes are roughly the same across all the nodes, indicating uniform load across the nodes.

1. To view cluster metrics, select the **Performance** tab and choose **Metrics**.

    ![Performance charts for 3 nodes](/images/ce/transactions_cloud_chart.png)

    The cluster [Performance metrics](../../../yugabyte-cloud/cloud-monitor/overview/) provide a customizable display of metrics, including YSQL operations per second and YSQL average latency.

To view the latency and throughput from the application side, navigate to the [simulation application UI](http://127.0.0.1:8000/).

![Latency and throughput with 3 nodes](/images/ce/simulation-graph-cloud.png)

## Add a node and observe linear scale-out

To add a node to the cluster, do the following:

1. On the **Settings** tab or under **Actions**, choose **Edit Infrastructure** to display the **Edit Infrastructure** dialog.

1. Change the number of nodes to **4** to add a new node.

1. Click **Confirm and Save Changes** when you are done.

    The scaling operation can take several minutes, during which time some cluster operations will not be available.

1. After the scaling operation completes, verify the details by selecting the **Nodes** tab.

    ![Read and write IOPS with 4 nodes](/images/ce/add-node-cloud.png)

    Shortly, you should see the new node performing a comparable number of reads and writes as the other nodes.

    The cluster automatically lets the client know to use the newly added node for serving queries. This scaling out of client queries is completely transparent to the application logic, allowing the application to scale linearly for both reads and writes.

1. Select the **Performance** tab.

    ![Latency and throughput graph with 4 nodes](/images/ce/add-node-cloud-chart.png)

    You should see a slight spike and drop in the latency and YSQL Operations/Sec when the node is added, and then both return to normal.

Navigate to the [simulation application UI](http://127.0.0.1:8000/) to see the new node added to the network diagram. You can also notice a slight spike and drop in the latency and throughput when the node is added, and then both return to normal, as shown in the following illustration:

![Latency and throughput graph with 4 nodes](/images/ce/add-node-graph-cloud.png)

## Remove a node and observe linear scale in

To remove a node from the cluster, do the following:

1. On the **Settings** tab or under **Actions**, choose **Edit Infrastructure** to display the **Edit Infrastructure** dialog.

1. Change the number of nodes to **3** to remove a node.

1. Click **Confirm and Save Changes** when you are done.

    The scaling operation can take several minutes, during which time some cluster operations will not be available.

1. After the scaling operation completes, verify the details by selecting the **Nodes** tab.

    ![Read and write IOPS with 4th node dead](/images/ce/stop-node.png)

    The load moves off the removed node and is redistributed to the other nodes.

1. Select the **Performance** tab.

    ![Performance metrics with 4th node dead](/images/ce/stop-node-chart.png)

    You should see a slight spike and drop in the latency and YSQL Operations/Sec when the node is added, and then both return to normal.

Navigate to the [simulation application UI](http://127.0.0.1:8000/) to see the node is removed from the network diagram. The network diagram may take few minutes to update.

You can also notice a slight spike and drop in the latency and throughput, both of which resume immediately as follows:

![Latency and throughput graph after stopping node 4](/images/ce/stop-node-graph-cloud.png)

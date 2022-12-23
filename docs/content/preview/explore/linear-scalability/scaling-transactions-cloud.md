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

This tutorial shows how YugabyteDB can scale seamlessly while running a read-write workload. Using the [YB Workload Simulator application](https://github.com/YugabyteDB-Samples/yb-workload-simulator) against a single region three-node cluster on YugabyteDB Managed with a replication factor of 3, you add a node while the workload is running. Using the built-in metrics, you can observe how the cluster scales out by verifying that the number of read and write IOPS are evenly distributed across all the nodes at all times.

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

{{% explore-setup-multi-cloud %}}

Follow the setup instructions to start a three-node cluster in YugabyteDB Managed, connect the YB Workload Simulator application, and run a read-write workload. To verify that the application is running correctly, navigate to the application UI at <http://localhost:8080/> to view the cluster network diagram and Latency and Throughput charts for the running workload.

## Observe IOPS per node

To view a table of per-node statistics for the cluster, in YugabyteDB Managed, do the following:

1. On the **Clusters** page, select the cluster.

1. Select **Nodes** to view the total read and write IOPS per node and other statistics as shown in the following illustration. Note that both the reads and the writes are roughly the same across all the nodes, indicating uniform load across the nodes.

![Read and write IOPS with 3 nodes](/images/ce/transactions_cloud_observe1.png)

To view your cluster metrics such as YSQL operations/second and Latency, in YugabyteDB Managed, select the cluster [Performance](/preview/yugabyte-cloud/cloud-monitor/overview/#performance-metrics) tab. You should see similar charts as shown in the following illustration:

![Performance charts for 3 nodes](/images/ce/transactions_cloud_chart.png)

To view the latency and throughput on the cluster while the workload is running, navigate to the [simulation application UI](http://127.0.0.1:8000/).

![Latency and throughput with 3 nodes](/images/ce/simulation-graph-cloud.png)

## Add node and observe linear scale-out

You can add a node to the cluster in YugabyteDB Managed as follows:

1. On the cluster **Settings** tab or under **Actions**, choose **Edit Infrastructure** to display the **Edit Infrastructure** dialog.

1. Change the number of nodes to 4 to add a new node.

1. Click **Confirm and Save Changes** when you are done.

The scaling operation can take several minutes, during which time some cluster operations are not available.

Verify that the node has been added on the cluster **Nodes** tab.

![Read and write IOPS with 4 nodes](/images/ce/add-node-cloud.png)

Shortly, you should see the new node performing a comparable number of reads and writes as the other nodes. The tablets are also distributed evenly across all the 4 nodes.

The cluster automatically lets the client know to use the newly added node for serving queries. This scaling out of client queries is completely transparent to the application logic, allowing the application to scale linearly for both reads and writes.

Navigate to the [Performance](/preview/yugabyte-cloud/cloud-monitor/overview/) tab to notice a slight spike and drop in the latency and YSQL Operations/Sec charts when the node is added, and then both return to normal, as shown in the following illustration:

![Latency and throughput graph with 4 nodes](/images/ce/add-node-cloud-chart.png)

Alternatively, you can navigate to the [simulation application UI](http://127.0.0.1:8000/) to see the new node being added to the network diagram. You can also notice a slight spike and drop in the latency and throughput when the node is added, and then both return to normal, as shown in the following illustration:

![Latency and throughput graph with 4 nodes](/images/ce/add-node-graph-cloud.png)

## Remove node and observe linear scale in

You can remove a node from the cluster in YugabyteDB Managed as follows:

1. On the cluster **Settings** tab or under **Actions**, choose **Edit Infrastructure** to display the **Edit Infrastructure** dialog.

1. Change the number of nodes to **3** to remove a node.

1. Click **Confirm and Save Changes** when you are done.

Again, the scale operation can take several minutes, during which time some cluster operations are not available.

Verify the details by selecting the **Nodes** tab. The load is moved off the removed node and redistributed to the other nodes.

Navigate to the **Performance** tab to notice a slight spike and drop in the latency and YSQL Operations/Sec charts when the node is added, and then both return to normal, as shown in the following illustration:

![Performance metrics with 4th node dead](/images/ce/stop-node-chart.png)

Alternatively, you can navigate to the [simulation application UI](http://127.0.0.1:8000/) to see the node being removed from the network diagram when it is stopped. Note that it may take few minutes to display the updated network diagram. You can also notice a slight spike and drop in the latency and throughput, both of which resume immediately as follows:

![Latency and throughput graph after stopping node 4](/images/ce/stop-node-graph-cloud.png)

## Clean up

To delete your cluster, access **Pause/Resume Cluster** and **Terminate Cluster** via the cluster **Actions** menu, or click the three dots icon for the cluster on the **Clusters** page.

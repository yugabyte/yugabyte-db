---
title: Scaling transactions
headerTitle: Scaling concurrent transactions
linkTitle: Scaling concurrent transactions
description: Scaling concurrent transactions in YugabyteDB.
headcontent: Horizontal scale-out and scale-in in YugabyteDB
menu:
  preview:
    name: Scaling transactions
    identifier: explore-transactions-scaling-transactions-1-ysql
    parent: explore-scalability
    weight: 210
type: docs
---

With YugabyteDB, you can add nodes to upscale your cluster efficiently and reliably to achieve more read and write IOPS (input/output operations per second), without any downtime.

This tutorial shows how YugabyteDB can scale seamlessly while running a read-write workload. Using the [YB Workload Simulator application](https://github.com/YugabyteDB-Samples/yb-workload-simulator) against a three-node cluster with a replication factor of 3, you add a node while the workload is running. Using the built-in metrics, you can observe how the cluster scales out by verifying that the number of read and write IOPS are evenly distributed across all the nodes at all times.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../scaling-transactions-cloud/" class="nav-link">
              <img src="/icons/cloud-icon.svg" alt="Icon">
Use a cloud cluster
    </a>
  </li>
  <li>
    <a href="../scaling-transactions/" class="nav-link active">
              <img src="/icons/server-iconsvg.svg" alt="Icon">
Use a local cluster
    </a>
  </li>
</ul>

{{% explore-setup-multi %}}

Follow the setup instructions to start a three-node cluster, connect the YB Workload Simulator application, and run a read-write workload. To verify that the application is running correctly, navigate to the application UI at <http://localhost:8080/> to view the cluster network diagram and Latency and Throughput charts for the running workload.

## Observe IOPS per node

To view a table of per-node statistics for the cluster, navigate to the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page. The following illustration shows the total read and write IOPS per node. Note that both the reads and the writes are roughly the same across all the nodes, indicating uniform load across the nodes.

![Read and write IOPS with 3 nodes](/images/ce/transactions_observe1.png)

To view the latency and throughput on the cluster while the workload is running, navigate to the [simulation application UI](http://127.0.0.1:8000/).

![Latency and throughput with 3 nodes](/images/ce/simulation-graph.png)

## Add node and observe linear scale-out

Add a node to the cluster with the same flags as follows:

```sh
$ ./bin/yugabyted start \
                --advertise_address=127.0.0.4 \
                --base_dir=/tmp/ybd4 \
                --cloud_location=aws.us-east.us-east-1a \
                --join=127.0.0.1
```

Now you should have 4 nodes. Refresh the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page to see the statistics update. Shortly, you should see the new node performing a comparable number of reads and writes as the other nodes. The tablets are also distributed evenly across all the 4 nodes.

The cluster automatically lets the client know to use the newly added node for serving queries. This scaling out of client queries is completely transparent to the application logic, allowing the application to scale linearly for both reads and writes.

![Read and write IOPS with 4 nodes](/images/ce/add-node-ybtserver.png)

Navigate to the [simulation application UI](http://127.0.0.1:8000/) to see the new node being added to the network diagram. You can also notice a slight spike and drop in the latency and throughput when the node is added, and then both return to normal, as shown in the following illustration:

![Latency and throughput graph with 4 nodes](/images/ce/add-node-graph.png)

## Remove node and observe linear scale in

Remove the recently added node from the cluster as follows:

```sh
$ ./bin/yugabyted stop \
                  --base_dir=/tmp/ybd4
```

Refresh the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page to see the statistics update. The `Time since heartbeat` value for that node will keep increasing. When that number reaches 60s (1 minute), YugabyteDB changes the status of that node from ALIVE to DEAD. Observe the load (tablets) and IOPS getting moved off the removed node and redistributed to the other nodes.

![Read and write IOPS with 4th node dead](/images/ce/stop-node-ybtserver.png)

Navigate to the [simulation application UI](http://127.0.0.1:8000/) to see the node being removed from the network diagram when it is stopped. Note that it may take about 60s (1 minute) to display the updated network diagram. You can also notice a slight spike and drop in the latency and throughput, both of which resume immediately as follows:

![Latency and throughput graph after stopping node 4](/images/ce/stop-node-graph.png)

## Clean up

To shut down the local cluster you created, do the following:

```sh
./bin/yugabyted destroy --base_dir=/tmp/ybd1
./bin/yugabyted destroy --base_dir=/tmp/ybd2
./bin/yugabyted destroy --base_dir=/tmp/ybd3
./bin/yugabyted destroy --base_dir=/tmp/ybd4
```

---
title: Scaling transactions
headerTitle: Scaling concurrent transactions
linkTitle: Scaling concurrent transactions
description: Scaling concurrent transactions in YugabyteDB.
headcontent: Horizontal scale-out and scale-in in YugabyteDB
menu:
  stable:
    name: Scaling transactions
    identifier: explore-transactions-scaling-transactions-1-ysql
    parent: explore-scalability
    weight: 200
type: docs
---

With YugabyteDB, you can add nodes to upscale your universe efficiently and reliably to achieve more read and write IOPS (input/output operations per second) without any downtime.

This document demonstrates how YugabyteDB can scale seamlessly while running a read-write workload. Using the [YB Workload Simulator application](https://github.com/YugabyteDB-Samples/yb-workload-simulator) against a three-node universe with a replication factor of 3, you add a node while the workload is running. Using the built-in metrics, you can observe how the universe scales out by verifying that the number of read and write IOPS are evenly distributed across all nodes at all times.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../scaling-transactions/" class="nav-link active">
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
    <a href="../scaling-transactions-yba/" class="nav-link">
      <img src="/icons/server.svg" alt="Server Icon">
      YugabyteDB Anywhere
    </a>
  </li>
</ul>

## Set up a universe

Follow the [setup instructions](../../#set-up-yugabytedb-universe) to start a local multi-node universe, connect the [YB Workload Simulator](../../#set-up-yb-workload-simulator) application, and run a read-write workload. To verify that the application is running correctly, navigate to the application UI at <http://localhost:8080/> to view the universe network diagram, as well as Latency and Throughput charts for the running workload.

## Observe IOPS per node

To view a table of per-node statistics for the universe, navigate to the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page. The following illustration shows the total read and write IOPS per node:

![Read and write IOPS with 3 nodes](/images/ce/transactions_observe1.png)

Notice that both reads and writes are approximately the same across all nodes, indicating uniform load across the nodes.

To view the latency and throughput on the universe while the workload is running, navigate to the [simulation application UI](http://127.0.0.1:8080/).

![Latency and throughput with 3 nodes](/images/ce/simulation-graph.png)

## Add a node

Add a node to the universe with the same flags, as follows:

```sh
./bin/yugabyted start \
                --advertise_address=127.0.0.4 \
                --base_dir=/tmp/ybd4 \
                --cloud_location=aws.us-east.us-east-1a \
                --join=127.0.0.1
```

Now you should have four nodes.

## Observe linear scale-out

Refresh the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page to see the statistics update. Shortly, you should see the new node performing a comparable number of reads and writes as the other nodes. The tablets are also distributed evenly across all four nodes.

The universe automatically lets the client know to use the newly-added node for serving queries. This scaling out of client queries is completely transparent to the application logic, allowing the application to scale linearly for both reads and writes:

![Read and write IOPS with 4 nodes](/images/ce/add-node-ybtserver.png)

Navigate to the [simulation application UI](http://127.0.0.1:8080/) to see the new node being added to the network diagram. You can also notice a slight spike and drop in the latency and throughput when the node is added, and then both return to normal, as shown in the following illustration:

![Latency and throughput graph with 4 nodes](/images/ce/add-node-graph.png)

## Remove a node

Remove the recently added node from the universe, as follows:

```sh
./bin/yugabyted stop \
                  --base_dir=/tmp/ybd4
```

## Observe linear scale-in

Refresh the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page to see the statistics update. The `Time since heartbeat` value for that node will keep increasing. When that number reaches 60s (1 minute), YugabyteDB changes the status of that node from ALIVE to DEAD. Observe the load (tablets) and IOPS getting moved off the removed node and redistributed to the other nodes:

![Read and write IOPS with 4th node dead](/images/ce/stop-node-ybtserver.png)

Navigate to the [simulation application UI](http://127.0.0.1:8080/) to see the node being removed from the network diagram when it is stopped. Note that it may take approximately 60s (1 minute) to display the updated network diagram. You can also notice a slight spike and drop in the latency and throughput, both of which resume immediately:

![Latency and throughput graph after stopping node 4](/images/ce/stop-node-graph.png)

{{% explore-cleanup-local %}}

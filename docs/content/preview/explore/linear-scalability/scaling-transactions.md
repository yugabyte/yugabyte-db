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
    weight: 200
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../scaling-transactions/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
<!--
  <li >
    <a href="../scaling-transactions-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
-->
</ul>

This page demonstrates YugabyteDB's horizontal scale-out and scale-in capability. With YugabyteDB, you can add nodes to upscale your cluster efficiently and reliably to achieve more read and write IOPS (input/output operations per second). This tutorial shows how YugabyteDB can scale while running a read-write workload. Using the prepackaged [YB Simulation Base Demo application](https://github.com/yugabyte/yb-simulation-base-demo-app) against a 3-node local cluster with a replication factor of 3, you add nodes while the workload is running. Next, you can observe how the cluster scales out by verifying that the number of read and write IOPS are evenly distributed across all the nodes at all times.

{{% explore-setup-multi %}}

## Run a generic workload

Using the YB Simulation Base Demo application, you can create and drop tables, load data, and run different workloads. The application updates charts of the latency and throughput on your YugabyteDB cluster in real time while the workloads are running.

To start a workload simulation against your cluster, do the following:

- Navigate to the application's UI at <http://localhost:8080>. The application displays a network diagram of your cluster topology, along with charts of latency and throughput as follows:

    ![Load UI with 3 nodes](/images/ce/load-app-ui.png)

- Click the **Active Workloads for Generic** menu and select **Usable operations**.
- Click **Create tables** and select **Run Create Tables Workload** to create tables.
- Click **Seed Data** and select **Run Seed Data Workload** to load data to the tables.
- Click **Simulation**, enable **Include new Inserts** and select **Run Simulation Workload** to start a simulation that performs read and write operations across all the nodes of your cluster.

You should see the latency and throughput graphs slowly starting to show up as follows:

![Latency and throughput for 3 nodes](/images/ce/simulation-graph.png)

Refer to the [YB Simulation Base Demo](https://github.com/yugabyte/yb-simulation-base-demo-app) repository for more details on creating and customizing your workloads.

## Observe IOPS per node

To view a table of per-node statistics for the cluster, navigate to the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page. The following illustration shows the total read and write IOPS per node. Note that both the reads and the writes are roughly the same across all the nodes, indicating uniform load across the nodes.

![Read and write IOPS with 3 nodes](/images/ce/transactions_observe1.png)

Observe that there is consistent latency and throughput as follows:

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

<!-- ![Read and write IOPS with 4 nodes - Rebalancing in progress](/images/ce/transactions_newnode_adding_observe.png) -->

![Read and write IOPS with 4 nodes](/images/ce/add-node-ybtserver.png)

You can notice a slight spike and drop in the latency and throughput when the node is added, but resumes back immediately as follows:

![Latency and throughput graph with 4 nodes](/images/ce/add-node-graph.png)

## Remove node and observe linear scale in

Remove the recently added node from the cluster as follows:

```sh
$ ./bin/yugabyted stop \
                  --base_dir=/tmp/ybd4
```

Refresh the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page to see the stats update. The `Time since heartbeat` value for that node will keep increasing. When that number reaches 60s (1 minute), YugabyteDB will change the status of that node from ALIVE to DEAD. Observe the load (tablets) and IOPS getting moved off the removed node and redistributed amongst the other nodes.

<!-- ![Read and write IOPS with 4th node dead](/images/ce/transactions_deleting_observe.png) -->

<!-- ![Read and write IOPS with 4th node removed](/images/ce/transactions_deleted_observe.png) -->

![Read and write IOPS with 4th node dead](/images/ce/stop-node-ybtserver.png)

You can notice a slight spike and drop in the latency and throughput when the node is stooped, but resumes back immediately as follows:

![Latency and throughput graph after stopping node 4](/images/ce/stop-node-graph.png)

## Clean up (optional)

Optionally, you can shut down the local cluster you created earlier.

```sh
./bin/yugabyted destroy --base_dir=/tmp/ybd1
./bin/yugabyted destroy --base_dir=/tmp/ybd2
./bin/yugabyted destroy --base_dir=/tmp/ybd3
./bin/yugabyted destroy --base_dir=/tmp/ybd4
```

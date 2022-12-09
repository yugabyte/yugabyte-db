---
title: Explore fault tolerance
headerTitle: Fault tolerance
linkTitle: Fault tolerance
description: Simulate fault tolerance and resilience in a local three-node YugabyteDB database cluster.
aliases:
  - /explore/fault-tolerance/
  - /preview/explore/fault-tolerance/
  - /preview/explore/cloud-native/fault-tolerance/
  - /preview/explore/postgresql/fault-tolerance/
  - /preview/explore/fault-tolerance-macos/
menu:
  preview:
    identifier: fault-tolerance-1-macos
    parent: explore
    weight: 215
type: docs
---

YugabyteDB can automatically handle failures and therefore provides [high availability](../../../architecture/core-functions/high-availability/). This tutorial demonstrates how YugabyteDB can continue to do reads and writes even in case of node failures. You create YSQL tables with a replication factor (RF) of 3, which allows a [fault tolerance](../../../architecture/docdb-replication/replication/) of 1. This means the cluster remains available for both reads and writes even if one node fails. However, if another node fails (bringing the number of failures to two), writes become unavailable on the cluster to preserve data consistency.

{{< note title="Setup" >}}

Local multi-node cluster. See [Set up your YugabyteDB cluster](../../../explore/#set-up-your-yugabytedb-cluster).

{{< /note >}}

Follow the setup instructions to start a three-node cluster, connect the YB Workload Simulator application, and run a read-write workload. To verify that the application is running correctly, navigate to the application UI at <http://localhost:8080/> to view the cluster network diagram and Latency and Throughput charts for the running workload.

## Observe even load across all nodes

To view a table of per-node statistics for the cluster, navigate to the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page. The following illustration shows the total read and write IOPS per node. Note that both the reads and the writes are roughly the same across all the nodes, indicating uniform load across the nodes.

![Read and write IOPS with 3 nodes](/images/ce/fault-tolerance-evenly-distributed.png)

To view the latency and throughput on the cluster while the workload is running, navigate to the [simulation application UI](http://127.0.0.1:8000/).

![Latency and throughput with 3 nodes](/images/ce/fault-tolerance-latency-throughput.png)

## Stop a node and observe continuous write availability

Stop one of the nodes to simulate the loss of a node as follows:

```sh
$ ./bin/yugabyted stop \
                  --base_dir=/tmp/ybd2
```

Refresh the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page to see the statistics update.

The `Time since heartbeat` value for that node will keep increasing. When that number reaches 60s (1 minute), YugabyteDB changes the status of that node from ALIVE to DEAD. Observe the load (tablets) and IOPS getting moved off the removed node and redistributed to the other nodes.

![Read and write IOPS with one node stopped](/images/ce/fault-tolerance-dead-node.png)

Navigate to the [simulation application UI](http://127.0.0.1:8000/) to see the node being removed from the network diagram when it is stopped. Note that it may take about 60s (1 minute) to display the updated network diagram. You can also notice a spike and drop in the latency and throughput, both of which resume immediately.

![Latency and throughput graph after dropping a node](/images/ce/fault-tolerance-latency-stoppednode.png)

Loss of the node has no impact on the application because no data is lost; previously replicated data on the remaining nodes is used to serve application requests.

## Clean up

You can shut down the local cluster you created as follows:

```sh
./bin/yugabyted destroy \
                --base_dir=/tmp/ybd1

./bin/yugabyted destroy \
                --base_dir=/tmp/ybd3
```

---
title: High availability during failures in a local universe
headerTitle: High availability during node and zone failures
linkTitle: HA during failures
description: Simulate fault tolerance and resilience in a local YugabyteDB database.
headcontent: Keep serving requests through node, rack, zone, and region failures
menu:
  preview:
    identifier: node-failure-1-macos
    parent: fault-tolerance
    weight: 10
type: docs
---

The ability to survive failures and be highly available is one of the foundational features of YugabyteDB. To better understand how YugabyteDB can continue to perform reads and writes even in case of node failures, run the following example.

## Scenario

Suppose you have a universe with a replication factor (RF) of 3, which allows a [fault tolerance](../../../architecture/docdb-replication/replication/#fault-tolerance) of 1. This means the universe remains available for both reads and writes even if a fault domain fails. However, if another were to fail (bringing the number of failures to two), writes would become unavailable in order to preserve data consistency.

{{<product-tabs list="local,anywhere">}}

## Set up a universe

Follow the [setup instructions](../../#set-up-yugabytedb-universe) to start a single region three-node universe, connect the [YB Workload Simulator](../../#set-up-yb-workload-simulator) application, and run a read-write workload. To verify that the application is running correctly, navigate to the application UI at <http://localhost:8080/> to view the universe network diagram, as well as latency and throughput charts for the running workload.

{{<note>}} The [YB Workload Simulator](../../#set-up-yb-workload-simulator) uses the [YugabyteDB JDBC Smart Driver](../../../drivers-orms/smart-drivers/) configured with connection load balancing. It automatically balances application connections across the nodes in a universe and re-balances connections when a node fails.{{</note>}}

## Observe even load across all nodes

To view a table of per-node statistics for the universe, navigate to the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page. The following illustration shows the total read and write IOPS per node:

![Read and write IOPS with 3 nodes](/images/ce/fault-tolerance-evenly-distributed.png)

Notice that both the reads and the writes are approximately the same across all nodes, indicating uniform load.

To view the latency and throughput on the universe while the workload is running, navigate to the [simulation application UI](http://127.0.0.1:8080/), as per the following illustration:

![Latency and throughput with 3 nodes](/images/ce/fault-tolerance-latency-throughput.png)

## Simulate a node failure

Stop one of the nodes to simulate the loss of a zone, as follows:

```sh
./bin/yugabyted stop --base_dir=/tmp/ybd2
```

## Observe workload remains available

Refresh the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page to see the statistics update.

The `Time since heartbeat` value for that node starts to increase. When that number reaches 60s (1 minute), YugabyteDB changes the status of that node from ALIVE to DEAD. Observe the load (tablets) and IOPS getting moved off the removed node and redistributed to the other nodes, as per the following illustration:

![Read and write IOPS with one node stopped](/images/ce/fault-tolerance-dead-node.png)

With the loss of the node, which also represents the loss of an entire fault domain, the universe is now in an under-replicated state.

Navigate to the [simulation application UI](http://127.0.0.1:8080/) to see the node removed from the network diagram when it is stopped, as per the following illustration:

![Latency and throughput graph after dropping a node](/images/ce/fault-tolerance-latency-stoppednode.png)

It may take close to 60 seconds to display the updated network diagram. You can also notice a spike and drop in the latency and throughput, both of which resume immediately.

Despite the loss of an entire fault domain, there is no impact on the application because no data is lost; previously replicated data on the remaining nodes is used to serve application requests.

{{% explore-cleanup-local %}}

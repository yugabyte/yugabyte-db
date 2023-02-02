---
title: Explore fault tolerance
headerTitle: Continuous availability
linkTitle: Continuous availability
description: Simulate fault tolerance and resilience in a local YugabyteDB database universe.
headcontent: Highly available and fault tolerant
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

YugabyteDB can continuously serve requests in the event of planned or unplanned outages, such as system upgrades and outages related to a node, availability zone, or region.

YugabyteDB provides [high availability](../../../architecture/core-functions/high-availability/) (HA) by replicating data across [fault domains](../../../architecture/docdb-replication/replication/#fault-domains). If a fault domain experiences a failure, an active replica is ready to take over as a new leader in a matter of seconds after the failure of the current leader and serve requests.

This is reflected in both the recovery point objective (RPO) and recovery time objective (RTO) for YugabyteDB universes:

- The RPO for the tablets in a YugabyteDB universe is 0, meaning no data is lost in the failover to another fault domain.
- The RTO for a zone outage is approximately 3 seconds, which is the time window for completing the failover and becoming operational out of the remaining fault domains.

<img src="/images/architecture/replication/rpo-vs-rto-zone-outage.png"/>

The benefits of continuous availability extend to performing maintenance and database upgrades. You can maintain and [upgrade your universe](../../../manage/upgrade-deployment/) to a newer version of YugabyteDB by performing a rolling upgrade; that is, stopping each node, upgrading the software, and restarting the node, with zero downtime for the universe as a whole.

For more information, see the following:

- [Continuous Availability with YugabyteDB video](https://www.youtube.com/watch?v=4PpiOMcq-j8)
- [Synchronous replication](../../../architecture/docdb-replication/replication/)

## Examples

The examples demonstrate how YugabyteDB can continue to perform reads and writes even in case of node failures. In this scenario, you create a universe with a replication factor (RF) of 3, which allows a [fault tolerance](../../../architecture/docdb-replication/replication/#fault-tolerance) of 1. This means the universe remains available for both reads and writes even if a fault domain fails. However, if another were to fail (bringing the number of failures to two), writes would become unavailable on the universe to preserve data consistency.

The examples are based on the YB Workload Simulator application, which uses the YugabyteDB JDBC [Smart Driver](../../../drivers-orms/smart-drivers/) configured with connection load balancing. The driver automatically balances application connections across the nodes in a universe and rebalances connections when a node fails.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../macos/" class="nav-link active">
      <img src="/icons/database.svg" alt="Server Icon">
      Local
    </a>
  </li>
  <li>
    <a href="../macos-yba/" class="nav-link">
      <img src="/icons/server.svg" alt="Server Icon">
      YugabyteDB Anywhere
    </a>
  </li>
</ul>

{{% explore-setup-multi %}}

Follow the [setup instructions](../../#set-up-yugabytedb-universe) to start a single region three-node universe, connect the [YB Workload Simulator](../../#set-up-yb-workload-simulator) application, and run a read-write workload. To verify that the application is running correctly, navigate to the application UI at <http://localhost:8080/> to view the universe network diagram, as well as latency and throughput charts for the running workload.

### Observe even load across all nodes

To view a table of per-node statistics for the universe, navigate to the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page. The following illustration shows the total read and write IOPS per node:

![Read and write IOPS with 3 nodes](/images/ce/fault-tolerance-evenly-distributed.png)

Notice that both the reads and the writes are approximately the same across all nodes, indicating uniform load.

To view the latency and throughput on the universe while the workload is running, navigate to the [simulation application UI](http://127.0.0.1:8080/), as per the following illustration:

![Latency and throughput with 3 nodes](/images/ce/fault-tolerance-latency-throughput.png)

### Stop node and observe continuous write availability

Stop one of the nodes to simulate the loss of a zone, as follows:

```sh
./bin/yugabyted stop --base_dir=/tmp/ybd2
```

Refresh the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page to see the statistics update.

The `Time since heartbeat` value for that node starts to increase. When that number reaches 60s (1 minute), YugabyteDB changes the status of that node from ALIVE to DEAD. Observe the load (tablets) and IOPS getting moved off the removed node and redistributed to the other nodes, as per the following illustration:

![Read and write IOPS with one node stopped](/images/ce/fault-tolerance-dead-node.png)

With the loss of the node, which also represents the loss of an entire fault domain, the universe is now in an under-replicated state.

Navigate to the [simulation application UI](http://127.0.0.1:8080/) to see the node removed from the network diagram when it is stopped, as per the following illustration:

![Latency and throughput graph after dropping a node](/images/ce/fault-tolerance-latency-stoppednode.png)

It may take close to 60 seconds to display the updated network diagram. You can also notice a spike and drop in the latency and throughput, both of which resume immediately.

Despite the loss of an entire fault domain, there is no impact on the application because no data is lost; previously replicated data on the remaining nodes is used to serve application requests.

### Clean up

You can shut down the local universe that you created as follows:

```sh
./bin/yugabyted destroy \
                --base_dir=/tmp/ybd1
./bin/yugabyted destroy \
                --base_dir=/tmp/ybd2
./bin/yugabyted destroy \
                --base_dir=/tmp/ybd3
```

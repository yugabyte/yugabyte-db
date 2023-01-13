---
title: Explore fault tolerance
headerTitle: Continuous availability
linkTitle: Continuous availability
description: Simulate fault tolerance and resilience in a local three-node YugabyteDB database cluster.
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

YugabyteDB is able to continuously serve requests in the event of planned or unplanned outages, such as system upgrades or node, availability zone, and regional outages.

YugabyteDB provides this [high availability](../../../architecture/core-functions/high-availability/) (HA) by replicating data across [fault domains](../../../architecture/docdb-replication/replication/#fault-domains). If a fault domain experiences a failure, an active replica is ready to take over as a new leader in a matter of seconds after the failure of the current leader and serve requests.

This is reflected in both the recovery point objective (RPO) and recovery time objective (RTO) for YugabyteDB clusters:

- The RPO for the tablets in a YugabyteDB cluster is 0, meaning no data is lost in the failover to another fault domain.
- The RTO for a zone outage is ~3 seconds, which is the time window for completing the failover and becoming operational out of the remaining fault domains.

<img src="/images/architecture/replication/rpo-vs-rto-zone-outage.png"/>

The benefits of continuous availability extend to performing maintenance and database upgrades. You can maintain and [upgrade your cluster](../../../manage/upgrade-deployment/) to a newer version of YugabyteDB by performing a rolling upgrade; that is, bringing each node down in turn, upgrading the software, and starting it up again, with zero downtime for the cluster as a whole.

## Example

This tutorial demonstrates how YugabyteDB can continue to do reads and writes even in case of node failures. In this scenario, you create a cluster with a replication factor (RF) of 3, which allows a [fault tolerance](../../../architecture/docdb-replication/replication/#fault-tolerance) of 1. This means the cluster remains available for both reads and writes even if a fault domain fails. However, if another were to fail (bringing the number of failures to two), writes would become unavailable on the cluster to preserve data consistency.

The tutorial uses the YB Workload Simulator application, which uses the YugabyteDB JDBC [Smart Driver](../../../drivers-orms/smart-drivers/) configured with connection load balancing. The driver automatically balances application connections across the nodes in a cluster, and re-balances connections when a node fails.

{{% explore-setup-multi %}}

Follow the setup instructions to start a three-node cluster, connect the YB Workload Simulator application, and run a read-write workload. To verify that the application is running correctly, navigate to the application UI at <http://localhost:8080/> to view the cluster network diagram and Latency and Throughput charts for the running workload.

### Observe even load across all nodes

To view a table of per-node statistics for the cluster, navigate to the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page. The following illustration shows the total read and write IOPS per node. Note that both the reads and the writes are roughly the same across all the nodes, indicating uniform load across the nodes.

![Read and write IOPS with 3 nodes](/images/ce/fault-tolerance-evenly-distributed.png)

To view the latency and throughput on the cluster while the workload is running, navigate to the [simulation application UI](http://127.0.0.1:8000/).

![Latency and throughput with 3 nodes](/images/ce/fault-tolerance-latency-throughput.png)

### Stop a node and observe continuous write availability

Stop one of the nodes to simulate the loss of a zone as follows:

```sh
$ ./bin/yugabyted stop --base_dir=/tmp/ybd2
```

Refresh the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page to see the statistics update.

The `Time since heartbeat` value for that node starts to increase. When that number reaches 60s (1 minute), YugabyteDB changes the status of that node from ALIVE to DEAD. Observe the load (tablets) and IOPS getting moved off the removed node and redistributed to the other nodes.

![Read and write IOPS with one node stopped](/images/ce/fault-tolerance-dead-node.png)

With the loss of the node, which also represents the loss of an entire fault domain, the cluster is now in an under-replicated state.

Navigate to the [simulation application UI](http://127.0.0.1:8000/) to see the node removed from the network diagram when it is stopped. Note that it may take about 60s (1 minute) to display the updated network diagram. You can also notice a spike and drop in the latency and throughput, both of which resume immediately.

![Latency and throughput graph after dropping a node](/images/ce/fault-tolerance-latency-stoppednode.png)

Despite the loss of an entire fault domain, there is no impact on the application because no data is lost; previously replicated data on the remaining nodes is used to serve application requests.

### Clean up

You can shut down the local cluster you created as follows:

```sh
./bin/yugabyted destroy \
                --base_dir=/tmp/ybd1

./bin/yugabyted destroy \
                --base_dir=/tmp/ybd3
```

## Learn more

- YugabyteDB Friday Tech Talk: [Continuous Availability with YugabyteDB](https://www.youtube.com/watch?v=4PpiOMcq-j8)
- [Synchronous replication](../../../architecture/docdb-replication/replication/)

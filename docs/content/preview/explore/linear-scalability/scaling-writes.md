---
title: Writes
headerTitle: Scaling writes
linkTitle: Scaling writes
description: Writes scale horizontally in YugabyteDB as you add more nodes
headcontent: Writes scale horizontally in YugabyteDB as you add more nodes
menu:
  preview:
    identifier: scaling-writes
    parent: explore-scalability
    weight: 40
type: docs
---

Writes scale linearly in YugabyteDB as more nodes are added to the cluster. Let us see how this happens.

## Writes - How do they work

When a application, connected to a node sends a write request for a key say `UPDATE T SET V=2 WHERE K=5`, YugabyteDB first identifies the location of the tablet leader containing the row with the key specified (`K=5`). Once the location of the tablet leader is identified, the request is internally re-directed to the node containing the tablet leader for the requested key.

The leader replicates the write to the followers and then acknowledges the write back to the application. This replication to follower adds additional latency to the request. A simple `write` involves a maximum of just 2 nodes.

![How does a write work](/images/explore/scalability/scaling-write-working.png)

If multiple rows have to be fetched and are located in different tablets, various rows are internally fetched from various tablets located in different nodes. This redirection is completely transparent to the application.

![How does a write work](/images/explore/scalability/scaling-write-multiple-fetches.png)

## Sysbench workload

Here we show how writes scale horizontally in YugabyteDB using a sysbench workload of simple inserts. The cluster consisted of `m6i.4xlarge` instances and had `1024` connections. All requests had a latency of <10ms.

![Scaling with sysbench](/images/explore/scalability/scaling-writes-sysbench.png)

You can clearly see that with increase in the no.of nodes the no.of reads scales linearly.

## YSQL - 1 million writes/second

On a 100-node YugabyteDB cluster set up with c5.4xlarge instances (16 vCPUs at 3.3GHz) in a single zone, the cluster performed 1.26 million writes/second with 1.7ms latency.

The cluster configuration is shown in the following illustration.

![YSQL write latency](https://www.yugabyte.com/wp-content/uploads/2019/09/yugabyte-db-vs-aws-aurora-cockroachdb-benchmarks-5.png)

## YCQL - 1 million writes/second

On a 50-node YugabyteDB cluster on GCP using n1-standard-16 instances (16 vCPUs at 2.20GHz), YCQL clocked 1.2M writes/second with 3.1ms average latency.

The cluster configuration is shown in the following illustration.

![YCQL write latency](/images/explore/scalability/ycql_1million_writes.png)

## Learn more

- [TPC-C benchmark](../../../benchmark/tpcc-ysql)
- [Write I/O path](../../../architecture/core-functions/write-path/)
- [YugabyteDB Benchmarks](../../../benchmark)
- [Scaling: YugabyteDB vs Cockroach vs Aurora](https://www.yugabyte.com/blog/yugabytedb-vs-cockroachdb-vs-aurora/)

---
title: Scaling writes
headerTitle: Scaling writes
linkTitle: Scaling writes
description: Writes scale horizontally in YugabyteDB as you add more nodes
headcontent: Write performance when scaling horizontally
aliases:
  - /preview/architecture/core-functions/write-path
menu:
  preview:
    identifier: scaling-writes
    parent: explore-scalability
    weight: 40
type: docs
---

Writes scale linearly in YugabyteDB as more nodes are added to the cluster. [Write](../../../architecture/transactions/single-row-transactions/) operations are more involved than [reads](../scaling-reads). This is because the write operation has to be replicated to a quorum before it is acknowledged to the application. Although writes are internally considered as transactions, YugabyteDB has a lot of [optimizations for single-row transactions](../../../architecture/transactions/single-row-transactions) and achieves high performance.

Let's go over how writes work and see how well they scale in YugabyteDB.

## How writes work

When an application connected to a node sends a write request for a key, YugabyteDB first identifies the location of the tablet leader containing the row with the key specified. After the location of the tablet leader is identified, the request is internally re-directed to the node containing the tablet leader for the requested key.

In the following illustration, you can see that the application sent the request for `UPDATE K=5` to `NODE-2`. The system identified that the key `K=5` is located in `NODE-1` and internally redirected the request to that node.

![How does a write work](/images/explore/scalability/scaling-write-working.png)

The leader replicates the write to the followers, updates any indexes if needed, and then acknowledges the write back to the application. The replication to followers adds additional latency to the request. A basic write involves a maximum of just 2 nodes.

In the following illustration, you can see that the leader `T2` in `NODE-1` for key `K=5`, replicates the update request to its followers in `NODE-2` and `NODE-4`.

![How does a write work](/images/explore/scalability/scaling-write-multiple-fetches.png)

If multiple rows have to be fetched and are located in different tablets, various rows are internally fetched from various tablets located in different nodes. This redirection is completely transparent to the application.

## Sysbench workload

The following shows how writes scale horizontally in YugabyteDB using a [Sysbench](../../../benchmark/sysbench-ysql/) workload of basic inserts. The cluster consisted of m6i.4xlarge instances and had 1024 connections. All requests had a latency of less than 10ms.

![Scaling with Sysbench](/images/explore/scalability/scaling-writes-sysbench.png)

You can clearly see that with an increase in the number of nodes, the number of writes scales linearly.

## YSQL - 1 million writes/second

On a 100-node YugabyteDB cluster using c5.4xlarge instances (16 vCPUs at 3.3GHz) in a single zone, the cluster performed 1.26 million writes/second with 1.7ms latency.

The cluster configuration is shown in the following illustration.

![YSQL write latency](https://www.yugabyte.com/wp-content/uploads/2019/09/yugabyte-db-vs-aws-aurora-cockroachdb-benchmarks-5.png)

## YCQL - 1 million writes/second

On a 50-node YugabyteDB cluster on GCP using n1-standard-16 instances (16 vCPUs at 2.20GHz), YCQL clocked 1.2M writes/second with 3.1ms average latency.

The cluster configuration is shown in the following illustration.

![YCQL write latency](/images/explore/scalability/ycql_1million_writes.png)

## Learn more

- [TPC-C benchmark](../../../benchmark/tpcc/)
- [YugabyteDB Benchmarks](../../../benchmark)
- [Scaling: YugabyteDB vs Cockroach vs Aurora](https://www.yugabyte.com/blog/yugabytedb-vs-cockroachdb-vs-aurora/)

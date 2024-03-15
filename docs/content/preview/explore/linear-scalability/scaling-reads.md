---
title: Scaling reads
headerTitle: Scaling reads
linkTitle: Scaling reads
description: Scaling reads in YugabyteDB.
headcontent: Read performance when scaling horizonatally
menu:
  preview:
    identifier: scaling-reads
    parent: explore-scalability
    weight: 30
type: docs
---

Reads scale linearly in YugabyteDB as more nodes are added to the cluster.

## How reads work

When an application connected to a node sends a read request for a key, say `SELECT * FROM T WHERE K=5`, YugabyteDB first identifies the location of the tablet leader containing the row with the key specified (`K=5`). After the location of the tablet leader is identified, the request is internally re-directed to the node containing the tablet leader for the requested key. The leader has the latest data and responds immediately.

A basic `select` statement involves a maximum of just 2 nodes. This redirection is completely transparent to the application.

![How does a read work](/images/explore/scalability/scaling-reads-redirection.png)

Multiple applications can connect to any node and the reads will be redirected correctly.

## Sysbench workload

The following shows how reads scale horizontally in YugabyteDB using a Sysbench workload of basic selects. The cluster consisted of m6i.4xlarge instances and had 1024 connections. All requests had a latency of less than 3ms.

![Scaling with Sysbench](/images/explore/scalability/scaling-reads-sysbench.png)

You can clearly see that with an increase in the number of nodes, the number of reads scales linearly.

## Learn more

- [Sysbench benchmark](../../../benchmark/sysbench-ysql/)
- [YugabyteDB Benchmarks](../../../benchmark)
- [Scaling: YugabyteDB vs Cockroach vs Aurora](https://www.yugabyte.com/blog/yugabytedb-vs-cockroachdb-vs-aurora/)

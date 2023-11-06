---
title: Scaling reads
headerTitle: Scaling reads
linkTitle: Scaling reads
description: Scaling reads in YugabyteDB.
headcontent: Scale reads horizonatally by adding more nodes
menu:
  preview:
    identifier: scaling-reads
    parent: explore-scalability
    weight: 30
type: docs
---

Reads scale linearly in YugabyteDB as more nodes are added to the cluster. To understand how this happens, let us go over some illustrations.

## Reads - How they work

When a application, connected to a node sends a read request for a key say `SELECT * FROM T WHERE K=5`, YugabyteDB first identifies the location of the tablet leader containing the row with the key specified (`K=5`). Once the location of the tablet leader is identified, the request is internally re-directed to the node containing the tablet leader for the requested key. The leader has the latest data nad responds immediatley. A simple `select` involves a maximum of just 2 nodes. This redirection is completely transparent to the application.

![How does a read work](/images/explore/scalability/scaling-reads-redirection.png)

Multiple applications can connect to any node and the reads will be redirected correctly.

## Sysbench workload

Here we show how reads scale horizontally in YugabyteDB using a sysbench workload of simple selects. The cluster consisted of `m6i.4xlarge` instances and had `1024` connections. All requests had a latency of <3ms.

![Scaling with sysbench](/images/explore/scalability/scaling-reads-sysbench.png)

You can clearly see that with increase in the no.of nodes the no.of reads scales linearly.

## Learn more

- [Sysbench benchmark](../../../benchmark/sysbench)
- [YugabyteDB Benchmarks](../../../benchmark)
- [Scaling: YugabyteDB vs Cockroach vs Aurora](https://www.yugabyte.com/blog/yugabytedb-vs-cockroachdb-vs-aurora/)

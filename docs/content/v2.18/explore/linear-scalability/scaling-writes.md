---
title: Writes
headerTitle: Writes
linkTitle: Writes
description: Writes in YugabyteDB.
headcontent: Write performance when scaling out
menu:
  v2.18:
    name: Writes
    identifier: scaling-writes
    parent: explore-scalability
    weight: 50
type: docs
---

Writes scale linearly in YugabyteDB as more nodes are added to the cluster.

In YugabyteDB, the rows are [sharded into tablets](../../../architecture/docdb-sharding/sharding/) based on the primary key (or the row ID when a primary key is not defined), and these tablets are distributed across the nodes in the cluster. Because data is distributed across the different nodes in the cluster, YugabyteDB can perform parallel write operations, which results in linear scaling of performance when nodes are added to the cluster.

YugabyteDB can clock more than 1 million writes/second in both [YSQL](../../../api/ysql/) and [YCQL](../../../api/ycql/) APIs.

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

---
title: Scaling writes
headerTitle: Scaling writes
linkTitle: Scaling writes
description: Scaling writes in YugabyteDB.
headcontent: Horizontally scale writes in YugabyteDB
menu:
  preview:
    name: Scaling writes
    identifier: scaling-writes
    parent: explore-scalability
    weight: 200
type: docs
---

Writes scale linearly in YugabyteDB as more nodes are added to the cluster. To understand how this happens, let us go over how data is distributed in YugabyteDB.

In YugabyteDB, the rows are [sharded into tablets](./sharding-data) based on the primary key (or the row-id when a primary key is not defined), and these tablets are distributed across various nodes in the system. Sharding ensures that the data is spread across the different nodes in the cluster. This enables parallel write operations to happen on different nodes in the cluster thereby leading to linear scaling with the addition of more nodes to the cluster.

YugabyteDB can easily clock more than `1M writes/s` in both [YSQL](../../../api/ysql/) and [YCQL](../../../api/ycql/) APIs.

## YSQL: 1 Million writes/s

We set up a 100-node YugabyteDB cluster with `c5.4xlarge` instances (`16` vCPUs @ `3.3GHz`) in a single zone. This cluster, aptly named MillionOps, is shown below.

![1.26 million writes/sec at 1.7ms latency](https://www.yugabyte.com/wp-content/uploads/2019/09/yugabyte-db-vs-aws-aurora-cockroachdb-benchmarks-5.png)

This cluster was able to perform **1.26** million writes/sec at **1.7ms** latency!

## YCQL: 1 Million writes/s

On a 50-node setup in GCP on `n1-standard-16` instances (`16` vCPUs @ `2.20GHz`), YCQL clocked `1.2M` write ops/sec with a `3.1ms` average latency.

![1.2M writes/s @ 3.1ms latency](/images/explore/scalability/ycql_1million_writes.png)

## Learn more

- [TPC-C benchmark](../../../benchmark/tpcc-ysql)
- [Write I/O path](../../../architecture/core-functions/write-path/)
- [YugabyteDB Benchmarks](../../../benchmark)
- [Scaling: YugabyteDB vs. Cockroach vs. Aurora](https://www.yugabyte.com/blog/yugabytedb-vs-cockroachdb-vs-aurora/)
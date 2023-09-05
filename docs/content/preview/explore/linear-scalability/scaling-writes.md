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
    weight: 100
type: docs
---

Writes scale linearly in YugabyteDB as more nodes are added to the cluster.

In YugabyteDB, the rows are [sharded into tablets](../sharding-data) based on the primary key (or the row-id when a primary key is not defined), and these tablets are distributed across various nodes in the cluster. Sharding ensures that the data is distributed across the different nodes in the cluster, enabling parallel write operations to happen on different nodes in the cluster, and thereby leading to linear scaling with the addition of more nodes to the cluster.

YugabyteDB can clock more than 1 million writes/second in both [YSQL](../../../api/ysql/) and [YCQL](../../../api/ycql/) APIs.

The following sections describe results from a cluster set up to run 1 million YSQL and YCQL.

## Results for 1 million YSQL writes/second

On a 100-node YugabyteDB cluster set up with `c5.4xlarge` instances (16 vCPUs at 3.3GHz) in a single zone, the cluster was able to perform **1.26** million writes/second at **1.7ms** latency.

The cluster details are described in the following illustration:

![YSQL write latency](https://www.yugabyte.com/wp-content/uploads/2019/09/yugabyte-db-vs-aws-aurora-cockroachdb-benchmarks-5.png)

## Results for 1 million YCQL writes/second

On a 50-node setup in GCP on `n1-standard-16` instances (16 vCPUs at 2.20GHz), YCQL clocked **1.2M** writes/second with a **3.1ms** average latency.

The cluster details are described in the following illustration:

![YCQL write latency](/images/explore/scalability/ycql_1million_writes.png)

## Learn more

- [TPC-C benchmark](../../../benchmark/tpcc-ysql)
- [Write I/O path](../../../architecture/core-functions/write-path/)
- [YugabyteDB Benchmarks](../../../benchmark)
- [Scaling: YugabyteDB vs Cockroach vs Aurora](https://www.yugabyte.com/blog/yugabytedb-vs-cockroachdb-vs-aurora/)

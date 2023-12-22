---
title: Simple workloads
headerTitle: Simple workloads
linkTitle: Simple workloads
description: Simple workloads
headcontent: Performance running simple workloads when scaling out
menu:
  preview:
    name: Simple workloads
    identifier: scaling-simple-workloads
    parent: explore-scalability
    weight: 70
type: docs
---

[DocDB](../../../architecture/docdb/), YugabyteDB's underlying distributed document store, uses a heavily customized version of RocksDB for node-local persistence. It has been engineered ground up to deliver high performance at a massive scale. Several features have been built into DocDB to support this design goal, including the following:

- Scan-resistant global block cache
- Bloom/index data splitting
- Global memstore limit
- Separate compaction queues to reduce read amplification
- Smart load balancing across disks

These features enable YugabyteDB to handle massive datasets with ease. The following scenario loads 1 billion rows into a 3-node cluster.

## 1 billion rows

This experiment uses the [YCSB benchmark](https://github.com/brianfrankcooper/YCSB/wiki) with the standard JDBC binding to load 1 billion rows. The expected dataset at the end of the data load is 1TB on each node (or a 3TB total data set size in each cluster).

| Cluster configuration |                        |
| --------------------: | :--------------------- |
|          Region | AWS us-west-2          |
|         Release | v2.1.5                 |
|             API | YSQL                   |
|           Nodes | 3                      |
|   Instance type | c5.4xlarge             |
|         Storage | 2 x 5TB SSDs (gp2 EBS) |
|        Sharding | Range and Hash         |
| Isolation level | Snapshot               |

## Results at a glance

1B row data load completed successfully for YSQL (using a range-sharded table) in about 26 hours. The following graphs show the throughput and average latency for the range-sharded load phase of the YCSB benchmark during the load phase.

![Ops and Latency](https://www.yugabyte.com/wp-content/uploads/2020/05/YugabyteDB-1B-data-load-completed-successfully-1024x369.png)

The total dataset was 3.2TB across the two nodes. Each node had just over 1TB of data.

![Total dataset](https://www.yugabyte.com/wp-content/uploads/2020/05/YugabyteDB-high-performance-with-low-SSTable-file-count-and-read-amplification.png)

## Learn more

- [YugabyteDB vs CockroachDB - 1B rows](https://www.yugabyte.com/blog/yugabytedb-vs-cockroachdb-bringing-truth-to-performance-benchmark-claims-part-2/)
- [YugabyteDB Benchmarks](../../../benchmark)
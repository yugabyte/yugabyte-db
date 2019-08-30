---
title: Architecture
linkTitle: Architecture
description: Architecture
menu:
  v1.0:
    identifier: faq-architecture
    parent: faq
    weight: 2480
---

## How can YugaByte DB be both CP and HA at the same time?

In terms of the [CAP theorem](https://blog.yugabyte.com/a-for-apple-b-for-ball-c-for-cap-theorem-8e9b78600e6d), YugaByte DB is a Consistent and Partition-tolerant (CP) database. It ensures High Availability (HA) for most practical situations even while remaining strongly consistent. While this may seem to be a violation of the CAP theorem, that is not the case. CAP treats availability as a binary option whereas YugaByte DB treats availability as a percentage that can be tuned to achieve high write availability (reads are always available as long as a single node is available). 

- During network partitions or node failures, the replicas of the impacted tablets (whose leaders got partitioned out or lost) form two groups: a majority partition that can still establish a Raft consensus and a minority partition that cannot establish such a consensus (given the lack of quorum). The replicas in the majority partition elect a new leader among themselves in a matter of seconds and are ready to accept new writes after the leader election completes. For these few seconds till the new leader is elected, the DB is unable to accept new writes given the design choice of prioritizing consistency over availabililty. All the leader replicas in the minority partition lose their leadership during these few seconds and hence become followers. 

- Majority partitions are available for both reads and writes. Minority partitions are available for reads only (even if the data may get stale as time passes) but not available for writes. **Multi-active availability** refers to YugaByte DB's ability to serve writes on any node of a non-partitioned cluster and reads on any node of a partitioned cluster.

- The above approach obviates the need for any unpredictable background anti-entropy operations as well as need to establish quorum at read time. As shown in the [YCSB benchmarks against Apache Cassandra](https://forum.yugabyte.com/t/ycsb-benchmark-results-for-yugabyte-and-apache-cassandra-again-with-p99-latencies/99), YugaByte DB delivers predictable p99 latencies as well as 3x read throughput that is also timeline-consistent (given no quorum is needed at read time).

On one hand, YugaByte DB's storage and replication architecture is similar to that of [Google Cloud Spanner](https://cloudplatform.googleblog.com/2017/02/inside-Cloud-Spanner-and-the-CAP-Theorem.html) which is also a CP database with high write availability. While Google Cloud Spanner leverages Google's proprietary network infrastructure, YugaByte DB is designed work on commodity infrastructure used by most enterprise users. On the other hand, YugaByte DB's multi-model, multi-API and tunable read latency approach is similar to that of [Azure Cosmos DB](https://azure.microsoft.com/en-us/blog/a-technical-overview-of-azure-cosmos-db/).

A post on our blog titled [Practical Tradeoffs in Google Cloud Spanner, Azure Cosmos DB and YugaByte DB](https://blog.yugabyte.com/practical-tradeoffs-in-google-cloud-spanner-azure-cosmos-db-and-yugabyte-db/) goes through the above tradeoffs in more detail.

## Why is a group of YugaByte DB nodes called a universe instead of the more commonly used term clusters?

The YugaByte universe packs a lot more functionality than what people think of when referring to a cluster. In fact, in certain deployment choices, the universe subsumes the equivalent of multiple clusters and some of the operational work needed to run these. Here are just a few concrete differences, which made us feel like giving it a different name would help earmark the differences and avoid confusion.

- A YugaByte universe can move into new machines/AZs/Regions/DCs in an online fashion, while these primitives are not associated with a traditional cluster.

- It is very easy to setup multiple async replicas with just a few clicks (in the Enterprise edition). This is built into the universe as a first-class operation with bootstrapping of the remote replica and all the operational aspects of running async replicas being supported natively. In the case of traditional clusters, the source and the async replicas are independent clusters. The user is responsible for maintaining these separate clusters as well as operating the replication logic.

- Failover to async replicas as the primary data and failback once the original datacenter is up and running are both natively supported within a universe.

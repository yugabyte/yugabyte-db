---
title: Compare CockroachDB with YugabyteDB
headerTitle: CockroachDB
linkTitle: CockroachDB
description: Compare CockroachDB with YugabyteDB in terms of database performance, PostgreSQL compatibility, and architecture.
aliases:
  - /comparisons/cockroachdb/
menu:
  preview_faq:
    parent: comparisons
    identifier: comparisons-cockroachdb
    weight: 1075
type: docs
---

YugabyteDB's sharding, replication, and transactions architecture is similar to CockroachDB in that both are inspired by the [Google Spanner design paper](https://research.google.com/archive/spanner-osdi2012.pdf). Additionally, both use Raft as the distributed consensus replication algorithm and RocksDB as the per-node storage engine. The following sections highlight the advantages and similarities YugabyteDB has when compared with CockroachDB.

## Advantages

YugabyteDB beats CockroachDB in the context of multiple developer benefits including higher performance for large data sizes, better PostgreSQL compatibility, more flexible geo-distributed deployment options as well as higher data density. Following blogs highlight the architectural and implementation advantages that make these benefits possible.

- [Bringing Truth to Competitive Benchmark Claims – YugabyteDB vs CockroachDB, Part 2](https://www.yugabyte.com/blog/yugabytedb-vs-cockroachdb-bringing-truth-to-performance-benchmark-claims-part-2/)

- [Distributed PostgreSQL on a Google Spanner Architecture – Storage Layer](https://www.yugabyte.com/blog/distributed-postgresql-on-a-google-spanner-architecture-storage-layer/)

- [Distributed PostgreSQL on a Google Spanner Architecture – Query Layer](https://www.yugabyte.com/blog/distributed-postgresql-on-a-google-spanner-architecture-query-layer/)

- [Yes We Can! Distributed ACID Transactions with High Performance](https://www.yugabyte.com/blog/yes-we-can-distributed-acid-transactions-with-high-performance/)

- [Enhancing RocksDB for Speed & Scale](https://www.yugabyte.com/blog/enhancing-rocksdb-for-speed-scale/)

## Similarities

Following blogs highlight how YugabyteDB works as an open-source, cloud-native Spanner derivative similar to CockroachDB.

- [Rise of Globally Distributed SQL Databases – Redefining Transactional Stores for Cloud Native Era](https://www.yugabyte.com/blog/rise-of-globally-distributed-sql-databases-redefining-transactional-stores-for-cloud-native-era/)

- [Implementing Distributed Transactions the Google Way: Percolator vs. Spanner](https://www.yugabyte.com/blog/implementing-distributed-transactions-the-google-way-percolator-vs-spanner/)

- [Google Spanner vs. Calvin: Is There a Clear Winner in the Battle for Global Consistency at Scale?](https://www.yugabyte.com/blog/google-spanner-vs-calvin-global-consistency-at-scale/)

- [Docker, Kubernetes and the Rise of Cloud Native Databases](https://www.yugabyte.com/blog/docker-kubernetes-and-the-rise-of-cloud-native-databases/)

- [Practical Tradeoffs in Google Cloud Spanner, Azure Cosmos DB and YugabyteDB](https://www.yugabyte.com/blog/practical-tradeoffs-in-google-cloud-spanner-azure-cosmos-db-and-yugabyte-db/)

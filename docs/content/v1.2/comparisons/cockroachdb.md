---
title: CockroachDB
linkTitle: CockroachDB
description: CockroachDB
block_indexing: true
menu:
  v1.2:
    parent: comparisons
    weight: 1075
---

YugabyteDB’s sharding, replication and transactions architecture is similar to that CockroachDB given that both are inspired by the [Google Spanner design paper](https://research.google.com/archive/spanner-osdi2012.pdf). Additionally, both use Raft as the distributed consensus replication algorithm and RocksDB as the per-node storage engine. The following sections highlight the advantages and similarities YugabyteDB has when compared with CockroachDB.

## Advantages

YugabyteDB beats CockroachDB in the context of multiple developer benefits including higher performance, stronger fit for internet-scale OLTP workloads, better PostgreSQL compatibility as well as higher data density. Following blogs highlight the architectural and implementation advantages that make these benefits possible.

- [YugabyteDB vs CockroachDB Performance Benchmarks for Internet-Scale Transactional Workloads](https://blog.yugabyte.com/yugabyte-db-vs-cockroachdb-performance-benchmarks-for-internet-scale-transactional-workloads/)

- [Distributed PostgreSQL on a Google Spanner Architecture – Storage Layer](https://blog.yugabyte.com/distributed-postgresql-on-a-google-spanner-architecture-storage-layer/) 

- [Distributed PostgreSQL on a Google Spanner Architecture – Query Layer](https://blog.yugabyte.com/distributed-postgresql-on-a-google-spanner-architecture-query-layer/) 

- [Yes We Can! Distributed ACID Transactions with High Performance](https://blog.yugabyte.com/yes-we-can-distributed-acid-transactions-with-high-performance/)

- [Enhancing RocksDB for Speed & Scale](https://blog.yugabyte.com/enhancing-rocksdb-for-speed-scale/)

## Similarities

Following blogs highlight how YugabyteDB works as an open source, cloud native Spanner derivative similar to CockroachDB.

- [Rise of Globally Distributed SQL Databases – Redefining Transactional Stores for Cloud Native Era](https://blog.yugabyte.com/rise-of-globally-distributed-sql-databases-redefining-transactional-stores-for-cloud-native-era/)

- [Implementing Distributed Transactions the Google Way: Percolator vs. Spanner](https://blog.yugabyte.com/implementing-distributed-transactions-the-google-way-percolator-vs-spanner/)

- [Google Spanner vs. Calvin: Is There a Clear Winner in the Battle for Global Consistency at Scale?](https://blog.yugabyte.com/google-spanner-vs-calvin-global-consistency-at-scale/)

- [Docker, Kubernetes and the Rise of Cloud Native Databases](https://blog.yugabyte.com/docker-kubernetes-and-the-rise-of-cloud-native-databases/)

- [Practical Tradeoffs in Google Cloud Spanner, Azure Cosmos DB and YugabyteDB](https://blog.yugabyte.com/practical-tradeoffs-in-google-cloud-spanner-azure-cosmos-db-and-yugabyte-db/)

## Download Benchmarking Report

[Download](https://www.yugabyte.com/yugabyte-db-vs-cockroachdb/) our comprehensive report that benchmarks YugabyteDB against CockroachDB while highlighting the architectural choices that enables YugabyteDB to have 3.5x better throughput and 3x lower latency.





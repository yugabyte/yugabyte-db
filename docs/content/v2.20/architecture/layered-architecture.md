---
title: Layered architecture for queries and storage
headerTitle: Layered architecture
linkTitle: Layered architecture
description: Learn about the layered architecture of YugabyteDB in the query layer and the storage layer.
menu:
  v2.20:
    identifier: architecture-layered-architecture
    parent: architecture
    weight: 1109
type: docs
---

YugabyteDB architecture follows a layered design. The logical layers are the Yugabyte Query Layer, and the DocDB distributed document store, as per the following diagram:

![YugabyteDB Logical Architecture](/images/architecture/yb-arch-new.png)

For more information, see the following:

* [YugabyteDB design goals](../design-goals/)
* [DocDB architecture](../docdb/)
* [Transactions in DocDB](../transactions/)
* [Query layer design](../query-layer/)
* [Core functions](../core-functions/)

## Yugabyte Query Layer

The [Yugabyte Query Layer](../query-layer/) (YQL) is the upper layer of YugabyteDB. Applications interact directly with YQL using client drivers. This layer deals with the API-specific aspects such as query and command compilation, as well as the run-time (data type representations, built-in operations, and so on). YQL is designed with extensibility in mind, allowing new APIs to be added.

YQL supports two types of distributed SQL APIs: Yugabyte SQL (YSQL) and Yugabyte Cloud QL (YCQL).

### YSQL

YSQL is a distributed SQL API that is built by reusing the PostgreSQL language layer code. It is a stateless SQL query engine that is wire-format compatible with PostgreSQL.

### YCQL

YCQL is a semi-relational language that has its roots in Cassandra Query Language. It is a SQL-like language built specifically to be aware of clustering of data across nodes.

For more information, see [The query layer design](../query-layer/overview/).

## DocDB

[DocDB](../docdb/) is a distributed document store with the following properties:

* [Strong write consistency](../docdb-replication/replication/#tablet-peers)
* Extreme resiliency to failures
* Automatic sharding and load balancing
* Zone-, region-, and cloud-aware data placement policies
* [Tunable read consistency](../docdb-replication/replication/#follower-reads)

Data in DocDB is stored in tables. Each table is composed of rows, with each row containing a key and a document.

### Sharding

Data is stored inside tables in DocDB. A DocDB table is often sharded into a number of tablets whose sharding is transparent.

For more information, see [DocDB sharding](../docdb-sharding/).

### Replication

Each tablet consisting of user data is replicated according to some replication factor using the Raft consensus algorithm. Replication is performed at a tablet level, and ensures single row linearizability even in the presence of failures.

For more information, see [DocDB replication](../docdb-replication/).

### Persistence

In order to persist data, a log-structured row- and document-oriented storage is used. It includes several optimizations for handling ever-growing data sets efficiently.

For more information, see [DocDB persistence](../docdb/persistence/).

### Transactions

DocDB supports both single-row and multi-row transactions, therefore allowing to modify multiple keys while preserving atomicity, consistency, isolation, and durability (ACID) properties.

For more information, see the following:

* [DocDB isolation levels](../transactions/isolation-levels/)
* [Single-row transactions](../transactions/single-row-transactions/)
* [Multi-row transactions](../transactions/distributed-txns/)

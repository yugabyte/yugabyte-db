---
title: Layered Architecture
linkTitle: Layered Architecture
description: Layered Architecture
aliases:
  - /latest/architecture/layered-architecture/
menu:
  latest:
    identifier: architecture-layered-architecture
    parent: architecture
    weight: 1110
isTocNested: true
showAsideToc: true
---

YugaByte DB architecture follows a layered design. It is comprised of 2 logical layers as shown in the diagram below:

* The **YugaByte Query Layer** or **YQL**
* A distributed document store called **DocDB**

![YugaByte DB Logical Architecture](/images/architecture/yb-arch-new.png)


## YugaByte Query Layer (YQL)

The [YugaByte Query Layer or YQL](../yql/) is the upper layer of YugaByte DB. Applications interact directly with YQL using client drivers. This layer deals with the API specific aspects such as query/command compilation and the run-time (data type representations, built-in operations and more). YQL is built with extensibility in mind, and allows for new APIs to be added.

Currently, YQL supports the server-side implementations of three different APIs - YSQL, YCQL and YEDIS.

#### YSQL

YSQL is an SQL API. It is built from PostgreSQL code. It is a stateless SQL query engine that is wire-format compatible with PostgreSQL.

#### YCQL

YCQL or the *YugaByte Cloud Query Language* is a semi-relational language that has its roots in Apache Cassandra's CQL. It is a SQL-like language built specifically to be aware of clustering of data across nodes.

#### YEDIS

YEDIS or the *YugaByte Dictionary Service* is a key to data-structure language that is wire-compatible with the Redis protocol.

{{< tip title="Read More" >}}
Understanding [the design of the query layer](../query-layer/overview/).
{{< /tip >}}


## DocDB

[DocDB](../docdb/) is a distributed document store. It has the following properties:

* [Strong write consistency](../docdb/replication/#strong-write-consistency)
* Resilient to failures
* High availability
* Automatic sharding and load-balancing
* Zone/region/cloud aware data placement policies
* [Tunable read consistency](../docdb/replication/#tunable-read-consistency)

Data in DocDB is stored in tables. Each table is composed of rows, each row contains a key and a document. Here are some key points:


#### Sharding

Data is stored inside tables in DocDB. A DocDB table is often sharded into a number of **tablets**. This sharding of tables is transparent to users.

You can read more about [how sharding works in DocDB](../docdb/sharding/).

#### Replication

Each tablet consisting of user data is replicated according to some replication factor using the Raft consensus algorithm. Replication is performed at a tablet level, and ensures single row linearizability even in the presence of failures.

You can read more about [how replication works in DocDB](../docdb/replication/).


#### Persistence

In order to persist data, a log-structured row/document-oriented storage is used. It includes several optimizations for handling ever-growing datasets efficiently.

You can read more about [how persistence of data works in DocDB](../docdb/persistence/).

#### Transactions Support

DocDB has support for both single-row and multi-row transactions. This means that DocDB allows modifying multiple keys while preserving ACID properties.

* Read about the [isolation levels supported in DocDB](../transactions/isolation-levels/).
* [Single-row transactions](../transactions/single-row-transactions/).
* [Multi-row transactions](../transactions/distributed-txns/).

## What's Next?

You can now read about the following:

{{< note title="" >}}
* [The design goals of YugaByte DB](../design-goals/)
* [Architecture of DocDB](../docdb/)
* [Transactions in DocDB](../transactions/)
* [Design of the query layer](../query-layer/)
* [How various functions work, like the read and write IO paths](../core-functions/)
{{< /note >}}


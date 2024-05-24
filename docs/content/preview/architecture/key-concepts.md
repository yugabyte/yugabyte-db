---
title: Key concepts
headerTitle: Key concepts
linkTitle: Key concepts
description: Learn about the Key concepts in YugabyteDB
headcontent: Glossary of key concepts
image: fa-sharp fa-thin fa-arrows-to-circle
aliases:
  - /preview/architecture/concepts
  - /preview/architecture/concepts/universe
  - /preview/architecture/concepts/single-node/
  - /preview/key-concepts/
menu:
  preview:
    identifier: architecture-concepts-universe
    parent: reference
    weight: 10
type: docs
---

## ACID

ACID stands for Atomicity, Consistency, Isolation, and Durability. These are a set of properties that guarantee that database transactions are processed reliably.

- Atomicity: All the work in a transaction is treated as a single atomic unit - either all of it is performed or none of it is.
- Consistency: A completed transaction leaves the database in a consistent internal state. This can either be all the operations in the transactions succeeding or none of them succeeding.
- Isolation: This property determines how and when changes made by one transaction become visible to the other. For example, a serializable isolation level guarantees that two concurrent transactions appear as if one executed after the other (that is, as if they occur in a completely isolated fashion).
- Durability: The results of the transaction are permanently stored in the system. The modifications must persist even in the instance of power loss or system failures.

YugabyteDB provides ACID guarantees for all [transactions](#transaction).

## CDC - Change data capture

CDC is a software design pattern used in database systems to capture and propagate data changes from one database to another in real-time or near real-time. YugabyteDB supports transactional CDC guaranteeing changes across tables are captured together. This enables use cases like real-time analytics, data warehousing, operational data replication, and event-driven architectures. {{<link "../docdb-replication/change-data-capture/">}}

## Cluster

A cluster is a group of [nodes](#node) on which YugabyteDB is deployed. The table data is distributed across the various [nodes](#node) in the cluster. Typically used as [*Primary cluster*](#primary-cluster) and [*Read replica cluster*](#read-replica-cluster).

{{<lead link="#universe">}}
Sometimes the term *cluster* is used interchangeably with the term *universe*. However, the two are not always equivalent, as described in [Universe](#universe).
{{</lead>}}

## DocDB

DocDB is the underlying document storage engine of YugabyteDB and is built on top of a highly customized and optimized verison of [RocksDB](http://rocksdb.org/). {{<link "../docdb">}}

## Fault domain

A fault domain is a potential point of failure. Examples of fault domains would be nodes, racks, zones, or entire regions. {{<link "../../../explore/fault-tolerance/#fault-domains">}}

## Hybrid time

Hybrid time/timestamp is a monotonically increasing timestamp derived using [Hybrid Logical clock](../transactions/transactions-overview/#hybrid-logical-clocks). Multiple aspects of YugabyteDB's transaction model are based on hybrid time. {{<link "../transactions/transactions-overview#hybrid-logical-clocks">}}

## Isolation levels

[Transaction](#transaction) isolation levels define the degree to which transactions are isolated from each other. Isolation levels determine how changes made by one transaction become visible to other concurrent transactions. {{<link "../../explore/transactions/isolation-levels/">}}

{{<tip>}}
YugabyteDB offers 3 isolation levels, [Serializable](../../explore/transactions/isolation-levels/#serializable-isolation), [Snapshot](../../explore/transactions/isolation-levels/#snapshot-isolation) and [Read committed](../../explore/transactions/isolation-levels/#read-committed-isolation) in the {{<product "ysql">}} API and one isolation level, [Snapshot](../../develop/learn/transactions/acid-transactions-ycql/) in the {{<product "ycql">}} API.
{{</tip>}}

## Leader balancing

YugabyteDB tries to keep the number of leaders evenly distributed across the [nodes](#node) in a cluster to ensure an even distribution of load.

## Leader election

Amongst the [tablet](#tablet) replicas, one tablet is elected [leader](#tablet-leader) as per the [Raft](../docdb-replication/raft) protocol. {{<link "../docdb-replication/raft/#leader-election">}}

## Master server

The [YB-Master](../yb-master/) service is responsible for keeping system metadata, coordinating system-wide operations, such as creating, altering, and dropping tables, as well as initiating maintenance operations such as load balancing. {{<link "../yb-master">}}

{{<tip>}}
The master server is also typically referred as just **master**.
{{</tip>}}

## MVCC

MVCC stands for Multi-version Concurrency Control. It is a concurrency control method used by YugabyteDB to provide access to data in a way that allows concurrent queries and updates without causing conflicts. {{<link "../transactions/transactions-overview/#hybrid-logical-clocks">}}

## Namespace

A namespace refers to a logical grouping or container for related database objects, such as tables, views, indexes, and other database constructs. Namespaces help organize and separate these objects, preventing naming conflicts and providing a way to control access and permissions.

A namespace in YSQL is referred to as a database and is logically identical to a namespace in other RDBMS (such as PostgreSQL).

 A namespace in YCQL is referred to as a keyspace and is logically identical to a keyspace in Apache Cassandra's CQL.

## Node

A node is a virtual machine, physical machine, or container on which YugabyteDB is deployed.

## OID

Object Identifier (OID) is a unique identifier assigned to each database object, such as tables, indexes, views, functions, and other system objects. They are assigned automatically and sequentially by the system when new objects are created.

While OIDs are an integral part of PostgreSQL's internal architecture, they are not always visible or exposed to users. In most cases, users interact with database objects using their names rather than their OIDs. However, there are cases where OIDs become relevant, such as when querying system catalogs or when dealing with low-level database operations.

{{<note>}}
OIDs are unique only within the context of a specific universe and are not guaranteed to be unique across different universes.
{{</note>}}

## Primary cluster

A primary cluster can perform both writes and reads, unlike a [read replica cluster](#read-replica-cluster), which can only serve reads. A [universe](#universe) can have only one primary cluster. Replication between [nodes](#node) in a primary cluster is performed synchronously.

## Raft

Raft stands for Replication for availability and fault tolerance. This is the algorithm that YugabyteDB uses for replication guaranteeing consistency. {{<link "../docdb-replication/replication/">}}

## Read replica cluster

Read replica clusters are optional clusters that can be set up in conjunction with a [primary cluster](#primary-cluster) to perform only reads; writes sent to read replica clusters get automatically rerouted to the primary cluster of the [universe](#universe). These clusters enable reads in regions that are far away from the primary cluster with timeline-consistent data. This ensures low latency reads for geo-distributed applications.

Data is brought into the read replica clusters through asynchronous replication from the primary cluster. In other words, [nodes](#node) in a read replica cluster act as Raft observers that do not participate in the write path involving the Raft leader and Raft followers present in the primary cluster. {{<link "../docdb-replication/read-replicas">}}

## Rebalancing

Rebalancing is the process of keeping an even distribution of tablets across the [nodes](#node) in a cluster. {{<link "../../explore/linear-scalability/data-distribution/#rebalancing">}}

## Region

A region refers to a defined geographical area or location where a cloud provider's data centers and infrastructure are physically located. Typically a region consists of one or more [zones](#zone). Examples of regions include `us-east-1` (Northern Virginia), `eu-west-1` (Ireland), and `us-central1` (Iowa).

## Replication factor (RF)

The number of copies of data in a YugabyteDB universe. YugabyteDB replicates data across zones (or fault domains) in order to tolerate faults. Fault tolerance (FT) and RF are correlated. To achieve a FT of k nodes, the universe has to be configured with a RF of (2k + 1).

The RF should be an odd number to ensure majority consensus can be established during failures. {{<link "../docdb-replication/replication/#replication-factor">}}

## Sharding

Sharding is the process of mapping a table row to a [tablet](#tablet). YugabyteDB supports 2 types of sharding, Hash and Range. {{<link "../docdb-sharding">}}

## Tablet

YugabyteDB splits a table into multiple small pieces called tablets for data distribution. The word "tablet" finds its origins in ancient history, when civilizations utilized flat slabs made of clay or stone as surfaces for writing and maintaining records. {{<link "../../explore/linear-scalability/data-distribution/">}}

{{<tip>}}
Tablets are also referred as shards.
{{</tip>}}

## Tablet leader

In a cluster, each [tablet](#tablet) is replicated as per the [replication factor](#replication-factor-rf) for high availability. Amongst these tablet replicas one tablet is elected as the leader and is responsible for handling writes and consistent reads. The other replicas as termed followers.

## Tablet splitting

When a tablet reaches a threshold size, it splits into 2 new [tablets](#tablet). This is a very quick operation. {{<link "../docdb-sharding/tablet-splitting">}}

## Transaction

A transaction is a sequence of operations performed as a single logical unit of work. YugabyteDB provides [ACID](#acid) guarantees for transactions. {{<link "/:version/explore/transactions">}}

## TServer

The [YB-TServer](../yb-tserver) service is responsible for maintaining and managing table data in the form of tablets, as well as dealing with all the queries. {{<link "../yb-tserver">}}

## Universe

A YugabyteDB universe comprises one primary cluster and zero or more read replica clusters that collectively function as a resilient and scalable distributed database.

{{<note>}}
Sometimes the terms *universe* and *cluster* are used interchangeably. However, the two are not always equivalent, as a universe can contain one or more [clusters](#cluster).
{{</note>}}

## xCluster

xCluster is a type of deployment where data is replicated asynchronously between two [universes](#universe) - a primary and a standby. The standby can be used for disaster recovery. YugabyteDB supports transactional xCluster {{<link "../docdb-replication/async-replication/">}}.

## YCQL

Semi-relational SQL API that is best fit for internet-scale OLTP and HTAP apps needing massive write scalability as well as blazing-fast queries. It supports distributed transactions, strongly consistent secondary indexes, and a native JSON column type. YCQL has its roots in the Cassandra Query Language. {{<link "../../api/ycql">}}

## YQL

The YugabyteDB Query Layer (YQL) is the primary layer that provides interfaces for applications to interact with using client drivers. This layer deals with the API-specific aspects such as query/command compilation and the run-time (data type representations, built-in operations, and more). {{<link "../query-layer">}}

## YSQL

Fully-relational SQL API that is wire compatible with the SQL language in PostgreSQL. It is best fit for RDBMS workloads that need horizontal write scalability and global data distribution while also using relational modeling features such as JOINs, distributed transactions, and referential integrity (such as foreign keys). Note that YSQL reuses the native query layer of the PostgreSQL open source project. {{<link "../../api/ysql">}}

## Zone

Typically referred as Availability Zones or just AZ, a zone is a datacenter or a group of colocated datacenters. Zone is the default [fault domain](#fault-domain) in YugabyteDB.

---
title: Key concepts
headerTitle: Key concepts
linkTitle: Key concepts
description: Learn about the Key concepts in YugabyteDB
headcontent: Glossary of key concepts
image: fa-sharp fa-thin fa-arrows-to-circle
aliases:
  - /preview/architecture/concepts/universe
  - /preview/architecture/concepts/single-node/
menu:
  preview:
    identifier: architecture-concepts-universe
    parent: reference
    weight: 10
type: docs
---

## CDC - Change data capture

CDC is a software design pattern used in database systems to capture and propagate data changes from one database to another in real-time or near real-time. YugabyteDB supports transactional CDC guaranteeing changes across tables are captured together. This enables use cases like real-time analytics, data warehousing, operational data replication, and event-driven architectures. {{<link "../docdb-replication/change-data-capture/">}}

## Cluster

A cluster is a group of [nodes](#node) on which YugabyteDB is deployed. The table data is distributed across the various [nodes](#node) in the cluster. Typically used as [*Primary cluster*](#primary-cluster) and [*Read replica cluster*](#read-replica-cluster).

{{<note>}}
Sometimes the term *cluster* is used interchangeably with the term *universe*. However, the two are not always equivalent, as described in [Universe](#universe).
{{</note>}}

## DocDB

DocDB is the underlying document storage engine of YugabyteDB and is built on top of a highly customized and optimized verison of [RocksDB](http://rocksdb.org/). {{<link "../docdb">}}

## Fault domain

A fault domain is a potential point of failure. Examples of fault domains would be nodes, racks, zones, or entire regions. {{<link "../../../explore/fault-tolerance/#fault-domains">}}

## Leader balancing

YugabyteDB tries to keep the number of leaders evenly distributed across the [nodes](#node) in a cluster to ensure an even distribution of load.

## Master server

The [YB-Master](../yb-master/) service is responsible for keeping system metadata, coordinating system-wide operations, such as creating, altering, and dropping tables, as well as initiating maintenance operations such as load balancing. {{<link "../yb-master">}}

{{<tip>}}
The master server is also typically referred as just **master**.
{{</tip>}}

## MVCC

MVCC stands for Multiversion Concurrency Control. It is a concurrency control method used by YugabyteDB to provide access to data in a way that allows concurrent queries and updates without causing conflicts. {{<link "../transactions/transactions-overview/#mvcc-using-hybrid-time">}}

## Namespace

A namespace refers to a logical grouping or container for related database objects, such as tables, views, indexes, and other database constructs. Namespaces help organize and separate these objects, preventing naming conflicts and providing a way to control access and permissions.

A namespace in YSQL is referred to as a database and is logically identical to a namespace in other RDBMS (such as PostgreSQL).

 A namespace in YCQL is referred to as a keyspace and is logically identical to a keyspace in Apache Cassandra's CQL.

## Node

A node is a virtual machine, physical machine, or container on which YugabyteDB is deployed.

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

## Tablet splitting

When a tablet reaches a threshold size, it splits into 2 new [tablets](#tablet). This is a very quick operation. {{<link "../docdb-sharding/tablet-splitting">}}

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

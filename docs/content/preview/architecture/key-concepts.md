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

CDC is a software design pattern used in database systems to capture and propagate data changes from one database to another in real-time or near real-time. YugabyteDB supports transactional CDC guaranteeing changes across tables are captured together. This enables use cases like real-time analytics, data warehousing, operational data replication, and event-driven architectures. {{<link dest="../docdb-replication/change-data-capture/">}}

## Cluster

A cluster is a group of [nodes](#node) on which YugabyteDB is deployed. The table data is distributed across the various [nodes](#node) in the cluster. Typically used as [*Primary cluster*](#primary-cluster) and [*Read replica cluster*](#read-replica-cluster).

{{<lead link="#universe">}}
Sometimes the term *cluster* is used interchangeably with the term *universe*. However, the two are not always equivalent, as described in [Universe](#universe).
{{</lead>}}

## DocDB

DocDB is the underlying document storage engine of YugabyteDB and is built on top of a highly customized and optimized verison of [RocksDB](http://rocksdb.org/). {{<link dest="../docdb">}}

## Fault domain

A fault domain is a potential point of failure. Examples of fault domains would be nodes, racks, zones, or entire regions. {{<link dest="../../explore/fault-tolerance/#fault-domains">}}

## Fault tolerance

YugabyteDB achieves resiliency by replicating data across fault domains using the Raft consensus protocol. The [fault domain](#fault-domain) can be at the level of individual nodes, availability zones, or entire regions.

The fault tolerance determines how resilient the cluster is to domain (that is, node, zone, or region) outages, whether planned or unplanned. Fault tolerance is achieved by adding redundancy, in the form of additional nodes, across the fault domain. Due to the way the Raft protocol works, providing a fault tolerance of `ft` requires replicating data across `2ft + 1` domains. This number is referred to as the [replication factor](#replication-factor-rf). For example, to survive the outage of 2 nodes, a cluster needs 2 * 2 + 1 nodes; that is, a replication factor of 5. While the 2 nodes are offline, the remaining 3 nodes can continue to serve reads and writes without interruption.

## Follower reads

Normally, only the [tablet leader](#tablet-leader) can process user-facing write and read requests. Follower reads allow you to lower read latencies by serving reads from the tablet followers. This is similar to reading from a cache, which can provide more read IOPS with low latency. The data might be slightly stale, but is timeline-consistent, meaning no out of order data is possible.

Follower reads are particularly beneficial in applications that can tolerate staleness. For instance, in a social media application where a post gets a million likes continuously, slightly stale reads are acceptable, and immediate updates are not necessary because the absolute number may not really matter to the end-user reading the post. In such cases, a slightly older value from the closest replica can achieve improved performance with lower latency. Follower reads are required when reading from [read replicas](#read-replica-cluster). {{<link dest="../../explore/going-beyond-sql/follower-reads-ysql/">}}

## Hybrid time

Hybrid time/timestamp is a monotonically increasing timestamp derived using [Hybrid Logical clock](../transactions/transactions-overview/#hybrid-logical-clocks). Multiple aspects of YugabyteDB's transaction model are based on hybrid time. {{<link dest="../transactions/transactions-overview#hybrid-logical-clocks">}}

## Isolation levels

[Transaction](#transaction) isolation levels define the degree to which transactions are isolated from each other. Isolation levels determine how changes made by one transaction become visible to other concurrent transactions. {{<link dest="../../explore/transactions/isolation-levels/">}}

{{<tip>}}
YugabyteDB offers 3 isolation levels - [Serializable](../../explore/transactions/isolation-levels/#serializable-isolation), [Snapshot](../../explore/transactions/isolation-levels/#snapshot-isolation) and [Read committed](../../explore/transactions/isolation-levels/#read-committed-isolation) - in the {{<product "ysql">}} API and one isolation level - [Snapshot](../../develop/learn/transactions/acid-transactions-ycql/) - in the {{<product "ycql">}} API.
{{</tip>}}

## Leader balancing

YugabyteDB tries to keep the number of leaders evenly distributed across the [nodes](#node) in a cluster to ensure an even distribution of load.

## Leader election

Amongst the [tablet](#tablet) replicas, one tablet is elected [leader](#tablet-leader) as per the [Raft](../docdb-replication/raft) protocol. {{<link dest="../docdb-replication/raft/#leader-election">}}

## Master server

The [YB-Master](../yb-master/) service is responsible for keeping system metadata, coordinating system-wide operations, such as creating, altering, and dropping tables, as well as initiating maintenance operations such as load balancing. {{<link dest="../yb-master">}}

{{<tip>}}
The master server is also typically referred as just **master**.
{{</tip>}}

## MVCC

MVCC stands for Multi-version Concurrency Control. It is a concurrency control method used by YugabyteDB to provide access to data in a way that allows concurrent queries and updates without causing conflicts. {{<link dest="../transactions/transactions-overview/#hybrid-logical-clocks">}}

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
OIDs are unique only in the context of a specific universe and are not guaranteed to be unique across different universes.
{{</note>}}

## Preferred region

By default, YugabyteDB distributes client requests equally across the regions in a cluster. If application reads and writes are known to be originating primarily from a single region, you can designate a preferred region, which pins the [tablet leaders](#tablet-leader) to that single region. As a result, the preferred region handles all read and write requests from clients. Non-preferred regions are used only for hosting tablet follower replicas.

Designating one region as preferred can reduce the number of network hops needed to process requests. For lower latencies and best performance, set the region closest to your application as preferred. If your application uses a [smart driver](#smart-driver), you can set the topology keys to target the preferred region. This means that the smart driver will distribute connections uniformly among the nodes in the preferred region, further optimizing performance.

Regardless of the preferred region setting, data is replicated across all the regions in the cluster to ensure region-level fault tolerance.

You can enable [follower reads](#follower-reads) to serve reads from non-preferred regions. In cases where the cluster has [read replicas](#read-replica-cluster) and a client connects to a read replica, reads are served from the replica; writes continue to be handled by the preferred region. {{<link dest="../../develop/build-global-apps/global-database/">}}

## Primary cluster

A primary cluster can perform both writes and reads, unlike a [read replica cluster](#read-replica-cluster), which can only serve reads. A [universe](#universe) can have only one primary cluster. Replication between [nodes](#node) in a primary cluster is performed synchronously.

## Raft

Raft stands for Replication for availability and fault tolerance. This is the algorithm that YugabyteDB uses for replication guaranteeing consistency. {{<link dest="../docdb-replication/replication/">}}

## Read replica cluster

Read replica clusters are optional clusters that can be set up in conjunction with a [primary cluster](#primary-cluster) to perform only reads; writes sent to read replica clusters get automatically rerouted to the primary cluster of the [universe](#universe). These clusters enable reads in regions that are far away from the primary cluster with timeline-consistent data. This ensures low latency reads for geo-distributed applications.

Data is brought into the read replica clusters through asynchronous replication from the primary cluster. In other words, [nodes](#node) in a read replica cluster act as Raft observers that do not participate in the write path involving the Raft leader and Raft followers present in the primary cluster. Reading from read replicas requires enabling [follower reads](#follower-reads). {{<link dest="../docdb-replication/read-replicas">}}

## Rebalancing

Rebalancing is the process of keeping an even distribution of tablets across the [nodes](#node) in a cluster. {{<link dest="../../explore/linear-scalability/data-distribution/#rebalancing">}}

## Region

A region refers to a defined geographical area or location where a cloud provider's data centers and infrastructure are physically located. Typically a region consists of one or more [zones](#zone). Examples of regions include `us-east-1` (Northern Virginia), `eu-west-1` (Ireland), and `us-central1` (Iowa).

## Replication factor (RF)

The number of copies of data in a YugabyteDB universe. YugabyteDB replicates data across [fault domains](#fault-domain) (for example, zones) in order to tolerate faults. [Fault tolerance](#fault-tolerance) (FT) and RF are correlated. To achieve a FT of k nodes, the universe has to be configured with a RF of (2k + 1).

The RF should be an odd number to ensure majority consensus can be established during failures. {{<link dest="../docdb-replication/replication/#replication-factor">}}

Each [read replica](#read-replica-cluster) cluster can also have its own replication factor. In this case, the replication factor determines how many copies of your primary data the read replica has; multiple copies ensure the availability of the replica in case of a node outage. Replicas *do not* participate in the primary cluster Raft consensus, and do not affect the fault tolerance of the primary cluster or contribute to failover.

## Sharding

Sharding is the process of mapping a table row to a [tablet](#tablet). YugabyteDB supports 2 types of sharding, Hash and Range. {{<link dest="../docdb-sharding">}}

## Smart driver

A smart driver in the context of YugabyteDB is essentially a PostgreSQL driver with additional "smart" features that leverage the distributed nature of YugabyteDB. These smart drivers intelligently distribute application connections across the nodes and regions of a YugabyteDB cluster, eliminating the need for external load balancers. This results in balanced connections that provide lower latencies and prevent hot nodes. For geographically-distributed applications, the driver can seamlessly connect to the geographically nearest regions and availability zones for lower latency.

Smart drivers are optimized for use with a distributed SQL database, and are both cluster-aware and topology-aware. They keep track of the members of the cluster as well as their locations. As nodes are added or removed from clusters, the driver updates its membership and topology information. The drivers read the database cluster topology from the metadata table, and route new connections to individual instance endpoints without relying on high-level cluster endpoints. The smart drivers are also capable of load balancing read-only connections across the available YB-TServers.
. {{<link dest="../../drivers-orms/smart-drivers/">}}

## Tablet

YugabyteDB splits a table into multiple small pieces called tablets for data distribution. The word "tablet" finds its origins in ancient history, when civilizations utilized flat slabs made of clay or stone as surfaces for writing and maintaining records. {{<link dest="../../explore/linear-scalability/data-distribution/">}}

{{<tip>}}
Tablets are also referred as shards.
{{</tip>}}

## Tablet follower

See [Tablet leader](#tablet-leader).

## Tablet leader

In a cluster, each [tablet](#tablet) is replicated as per the [replication factor](#replication-factor-rf) for high availability. Amongst these tablet replicas one tablet is elected as the leader and is responsible for handling writes and consistent reads. The other replicas are called followers.

## Tablet splitting

When a tablet reaches a threshold size, it splits into 2 new [tablets](#tablet). This is a very quick operation. {{<link dest="../docdb-sharding/tablet-splitting">}}

## Transaction

A transaction is a sequence of operations performed as a single logical unit of work. YugabyteDB provides [ACID](#acid) guarantees for transactions. {{<link dest="/:version/explore/transactions">}}

## TServer

The [YB-TServer](../yb-tserver) service is responsible for maintaining and managing table data in the form of tablets, as well as dealing with all the queries. {{<link dest="../yb-tserver">}}

## Universe

A YugabyteDB universe comprises one [primary cluster](#primary-cluster) and zero or more [read replica clusters](#read-replica-cluster) that collectively function as a resilient and scalable distributed database.

{{<note>}}
Sometimes the terms *universe* and *cluster* are used interchangeably. The two are not always equivalent, as a universe can contain one or more [clusters](#cluster).
{{</note>}}

## xCluster

xCluster is a type of deployment where data is replicated asynchronously between two [universes](#universe) - a primary and a standby. The standby can be used for disaster recovery. YugabyteDB supports transactional xCluster {{<link dest="../docdb-replication/async-replication/">}}.

## YCQL

Semi-relational SQL API that is best fit for internet-scale OLTP and HTAP apps needing massive write scalability as well as blazing-fast queries. It supports distributed transactions, strongly consistent secondary indexes, and a native JSON column type. YCQL has its roots in the Cassandra Query Language. {{<link dest="../../api/ycql">}}

## YQL

The YugabyteDB Query Layer (YQL) is the primary layer that provides interfaces for applications to interact with using client drivers. This layer deals with the API-specific aspects such as query/command compilation and the run-time (data type representations, built-in operations, and more). {{<link dest="../query-layer">}}

## YSQL

Fully-relational SQL API that is wire compatible with the SQL language in PostgreSQL. It is best fit for RDBMS workloads that need horizontal write scalability and global data distribution while also using relational modeling features such as JOINs, distributed transactions, and referential integrity (such as foreign keys). Note that YSQL reuses the native query layer of the PostgreSQL open source project. {{<link dest="../../api/ysql">}}

## Zone

Typically referred as Availability Zones or just AZ, a zone is a datacenter or a group of colocated datacenters. Zone is the default [fault domain](#fault-domain) in YugabyteDB.

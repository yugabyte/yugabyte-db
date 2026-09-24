---
title: Key concepts
headerTitle: Key concepts
linkTitle: Key concepts
description: Learn about the Key concepts in YugabyteDB
headcontent: Glossary of key concepts
menu:
  v2.20:
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

A fault domain is a potential point of failure. Examples of fault domains would be nodes, racks, zones, or entire regions. {{<link dest="../../../explore/fault-tolerance/#fault-domains">}}

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
YugabyteDB offers 3 isolation levels - [Serializable](../../explore/transactions/isolation-levels/#serializable-isolation), [Snapshot](../../explore/transactions/isolation-levels/#snapshot-isolation) and [Read committed](../../explore/transactions/isolation-levels/#read-committed-isolation) - in the {{<product "ysql">}} API and one isolation level - [Snapshot](/stable/develop/learn/transactions/acid-transactions-ycql/) - in the {{<product "ycql">}} API.
{{</tip>}}

## Leader balancing

YugabyteDB tries to keep the number of leaders evenly distributed across the [nodes](#node) in a cluster to ensure an even distribution of load. When [leader affinity](#leader-affinity) (preferred placement) is set, balancing applies only among replicas allowed at the current preferred rank. {{<link dest="../docdb-sharding/cluster-balancing/">}}

## Leader election

Raft elects one replica as leader in each replica group. For user [tablets](#tablet), that replica is the [tablet leader](#tablet-leader). For the [sys catalog](#sys-catalog), it is the [sys catalog leader](#sys-catalog-leader) (the active [master](#master-server)). {{<link dest="../docdb-replication/raft/#leader-election">}}

## Leader affinity

Leader affinity is the cluster policy that ranks zones so [tablet leaders](#tablet-leader) (and, by default, the [sys catalog leader](#sys-catalog-leader)) prefer those zones. The load balancer elects leaders onto healthy replicas in rank order; omitted zones are last-resort. This is the same mechanism as [preferred region](#preferred-region).

The policy is stored as `multi_affinitized_leaders` in cluster config. You set it with `yb-admin set_preferred_zones` or the YBA Preferred setting. It does not change replica placement.

Tablespace `leader_preference` sets the same kind of ranking for tables in that tablespace. It is stored with the tablespace, not in the cluster config, and it does not move the [sys catalog leader](#sys-catalog-leader).

[Leader balancing](#leader-balancing) still spreads leaders evenly, but only among replicas allowed at the current rank.

## Master server

The [YB-Master](../yb-master/) service keeps system metadata, coordinates DDL, and runs cluster-wide operations such as load balancing. Masters form a Raft group around the [sys catalog](#sys-catalog); the [sys catalog leader](#sys-catalog-leader) is the active master. {{<link dest="../yb-master">}}

Master process placement (which nodes run yb-master) is separate from who leads the sys catalog, the same distinction as for [tablet leaders](#tablet-leader). See [Sys catalog leader](#sys-catalog-leader).

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

By default, YugabyteDB balances [tablet leaders](#tablet-leader) across the regions in a cluster. If reads and writes originate primarily from one region, you can designate a preferred region (or ranked regions and zones), which pins tablet leaders there. Clients then send reads and writes to that region. If a [master](#master-server) is already running in a preferred zone, the [sys catalog leader](#sys-catalog-leader) steps down onto that master; ranking does not place master processes.

You can rank multiple regions so that if the first fails, leaders move to the next. For lower latency, prefer the region closest to the application. If the application uses a [smart driver](#smart-driver), set topology keys to target the preferred region so connections land on those nodes.

Follower copies stay in the other regions. Ranking chooses the leader among replicas that already exist; it does not add, remove, or move copies. See [Tablet leader](#tablet-leader).

You can enable [follower reads](#follower-reads) to serve reads from non-preferred regions. If the cluster has [read replicas](#read-replica-cluster) and a client connects to a replica, reads are served from the replica; writes continue to go to the preferred region's tablet leaders. {{<link dest="/stable/develop/build-global-apps/global-database/">}}

## Primary cluster

A primary cluster can perform both writes and reads, unlike a [read replica cluster](#read-replica-cluster), which can only serve reads. A [universe](#universe) can have only one primary cluster. Replication between [nodes](#node) in a primary cluster is performed synchronously.

## Raft

Raft stands for Replication for availability and fault tolerance. This is the algorithm that YugabyteDB uses for replication guaranteeing consistency. {{<link dest="../docdb-replication/replication/">}}

## Read replica cluster

Read replica clusters are optional clusters that can be set up in conjunction with a [primary cluster](#primary-cluster) to perform only reads; writes sent to read replica clusters get automatically rerouted to the primary cluster of the [universe](#universe). These clusters enable reads in regions that are far away from the primary cluster with timeline-consistent data. This ensures low latency reads for geo-distributed applications.

Data is brought into the read replica clusters through asynchronous replication from the primary cluster. In other words, [nodes](#node) in a read replica cluster act as Raft observers that do not participate in the write path involving the Raft leader and Raft followers present in the primary cluster. Reading from read replicas requires enabling [follower reads](#follower-reads). {{<link dest="../docdb-replication/read-replicas">}}

## Rebalancing

Rebalancing is the process of keeping an even distribution of tablets across the [nodes](#node) in a cluster. {{<link dest="../../explore/linear-scalability/data-distribution/#rebalancing">}}

For detailed information on cluster balancing scenarios, monitoring, and configuration, see [Cluster balancing](../docdb-sharding/cluster-balancing/).

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
. {{<link dest="/stable/develop/drivers-orms/smart-drivers/">}}

## Sys catalog

The sys catalog is a single [tablet](#tablet), replicated across the [master servers](#master-server), that stores cluster metadata: namespaces, tables, tablet locations, roles, and cluster config. The sys catalog is not the same as the PostgreSQL [system catalogs](../system-catalog/) (`pg_catalog`), which YSQL uses for SQL object metadata.

The Raft leader of that tablet is the [sys catalog leader](#sys-catalog-leader). The other masters are followers. The sys catalog is not on the user-table I/O path. {{<link dest="../yb-master">}}

## Sys catalog leader

The sys catalog leader is the Raft leader of the [sys catalog](#sys-catalog) tablet, and is the active [master](#master-server). It coordinates DDL, catalog lookups, and cluster operations such as load balancing.

As with [tablet leaders](#tablet-leader), a leader can only sit where a replica already exists. A [preferred region](#preferred-region) can step the sys catalog leader down onto a master already in a preferred zone; it does not move master processes. If no master is in a preferred zone, the sys catalog leader stays where it is.

## Tablet

YugabyteDB splits a table into multiple small pieces called tablets for data distribution. The word "tablet" finds its origins in ancient history, when civilizations utilized flat slabs made of clay or stone as surfaces for writing and maintaining records. {{<link dest="../../explore/linear-scalability/data-distribution/">}}

{{<tip>}}
Tablets are also referred as shards.
{{</tip>}}

## Tablet follower

See [Tablet leader](#tablet-leader).

## Tablet leader

In a cluster, each [tablet](#tablet) is replicated according to the [replication factor](#replication-factor-rf). One replica is elected leader and handles writes and strongly consistent reads. The others are followers.

Where copies live (replica placement) is separate from which copy is leader. A leader can only sit where a replica already exists. A [preferred region](#preferred-region) pins leaders onto those existing copies; it does not move or add replicas, and it does not pack all copies into the preferred region. Fault tolerance still comes from followers in other fault domains. Strong writes still wait for a majority of replicas, so write latency follows replica locations even after leaders are pinned.

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

---
title: Universe
headerTitle: Universe
linkTitle: Universe
description: Learn about the YugabyteDB universe (or cluster).
menu:
  stable:
    identifier: architecture-concepts-universe
    parent: key-concepts
    weight: 1122
type: docs
---

A YugabyteDB universe is a group of nodes (virtual machines, physical machines, or containers) that collectively function as a resilient and scalable distributed database.

Depending on business requirements and latency considerations, a universe can be deployed in a variety of configurations, such as the following:

- Single availability zone.
- Multiple availability zones in a region.
- Multiple regions, with synchronous and asynchronous replication choices.

Note that sometimes the terms *universe* and *cluster* are used interchangeably. However, the two are not always equivalent, as described in [Universe vs. cluster](#universe-vs-cluster).

## Organization of data

A YugabyteDB universe can consist of one or more namespaces. Each of these namespaces can contain one or more user tables.

YugabyteDB automatically shards, replicates, and load-balances these tables across the nodes in the universe, while respecting your intent such as cross availability zone (AZ) or region placement requirements, desired replication factor, and so on. YugabyteDB automatically handles failures, such as node, disk, AZ, or region failures, as well as redistributes and rereplicates data back to desired levels across the remaining available nodes while still respecting any replica placement requirements.

### YSQL

A namespace in YSQL is referred to as a database and is logically identical to a namespace in other RDBMS (such as PostgreSQL).

### YCQL

A namespace in YCQL is referred to as a keyspace and is logically identical to a keyspace in Apache Cassandra's CQL.

## Component services

A universe comprises of two sets of servers: YugabyteDB Tablet Server (YB-TServer) and YugabyteDB Master Server (YB-Master). The sets of YB-TServer and YB-Master servers form two respective distributed services using [Raft](https://raft.github.io/) as a building block. High availability (HA) of these services is achieved by the failure detection, leader election, and data replication mechanisms in the Raft implementation.

Note that YugabyteDB is designed to not have any single point of failure.

### YB-TServer

The [YB-TServer](../yb-tserver/) service is responsible for hosting and serving user data (for example, tables), as well as dealing with all the queries.

### YB-Master

The [YB-Master](../yb-master/) service is responsible for keeping system metadata, coordinating system-wide operations, such as creating, altering, and dropping tables, as well as initiating maintenance operations such as load balancing.

The following illustration depicts a basic four-node YugabyteDB universe:

![4 node cluster](/images/architecture/4_node_cluster.png)

## Universe vs. cluster

A YugabyteDB universe is comprised of one primary cluster and zero or more read replica clusters.

- A primary cluster can perform both writes and reads. Replication between nodes in a primary cluster is performed synchronously.

- Read replica clusters can perform only reads; writes sent to read replica clusters get automatically rerouted to the primary cluster of the universe. These clusters enable reads in regions that are far away from the primary cluster with timeline-consistent data. This ensures low latency reads for geo-distributed applications. Data is brought into the read replica clusters through asynchronous replication from the primary cluster. In other words, nodes in a read replica cluster act as Raft observers that do not participate in the write path involving the Raft leader and Raft followers present in the primary cluster.

For more information about read replica clusters, see [Read replicas](../../docdb-replication/read-replicas/).

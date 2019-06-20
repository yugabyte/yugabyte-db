---
title: Universe
linkTitle: Universe
description: Universe
aliases:
  - /latest/architecture/concepts/universe/
  - /latest/architecture/concepts/single-node/
menu:
  latest:
    identifier: architecture-concepts-universe
    parent: architecture-concepts
    weight: 1122
isTocNested: true
showAsideToc: true
---

A YugaByte DB universe, is a group of nodes (VMs, physical machines or containers) that collectively function as a highly available and resilient database.

{{< note title="Note" >}}
In most of the docs, the term `cluster` and `universe` are used interchangeably. However, the two are not always equivalent. The difference is described in a section below.
{{< /note >}}

The universe can be deployed in a variety of configurations depending on business requirements, and latency considerations. Some examples:

- Single availability zone (AZ/rack/failure domain)
- Multiple AZs in a region
- Multiple regions (with synchronous and asynchronous replication choices)

## Organization of User Data

A YugaByte DB *universe* can consist of one or more namespaces. Each of these namespaces can contain one or more user tables.

YugaByte automatically shards, replicates and load-balances these tables across the nodes in the universe, while respecting user-intent such as cross-AZ or region placement requirements, desired replication factor, and so on. YugaByte automatically handles failures (e.g., node, process, AZ or region failures), and re-distributes and re-replicates data back to desired levels across the remaining available nodes while still respecting any data placement requirements.

### YSQL

Namespaces in YSQL are referred to as **databases** and are logically the same as in other RDBMS databases (such as PostgreSQL).

### YCQL

A namespace in YCQL is referred to as a **keyspace** and is logically the same as a keyspace in Apache Cassandra's CQL. 

## Processes and Services

A universe comprises of two sets of processes, **YB-TServer** and **YB-Master**. The YB-TServer and YB-Master processes form two respective distributed services using [Raft](https://raft.github.io/) as a building block. High Availability (HA) of both these services is achieved by the failure-detection, leader election and data replication mechanisms in the Raft implementation.

{{< note title="Note" >}}
YugaByte DB is architected to not have any single point of failure.
{{< /note >}}

These serve different purposes as described below.

### YB-TServer Process

The **YB-TServer** (aka the *YugaByte DB Tablet Server*) processes are responsible for hosting/serving user data (e.g, tables). They deal with all the user queries.

You can read more [about YB-TServers](../yb-tserver).


### YB-Master Process

The **YB-Master** (aka the *YugaByte DB Master Server*) processes are responsible for keeping system metadata, coordinating system-wide operations such as create/alter drop tables, and initiating maintenance operations such as load-balancing.

You can read more [about YB-TServers](../yb-tserver).



Below is an illustration of a simple 4-node YugaByte universe:

![4 node cluster](/images/architecture/4_node_cluster.png)


## Universe vs Cluster

A YugaByte DB universe can comprise of one or more clusters. Each cluster is a logical group of nodes running YB-TServers that are either performing one of the following replication modes:

* Synchronous replication
* Asynchronous replication


The set of nodes that are performing strong replication are referred to as the **Primary cluster** and other groups are called **Read Replica clusters**. 

Note that:

* There is always one primary cluster in a universe
* There can be zero or more read replica clusters in that universe.

You can find more information about read replicas [here](../replication/#read-only-replicas).





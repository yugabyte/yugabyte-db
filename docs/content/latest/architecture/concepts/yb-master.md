---
title: YB-Master
linkTitle: YB-Master
description: YB-Master
aliases:
  - /latest/architecture/concepts/yb-master/
menu:
  latest:
    identifier: architecture-concepts-yb-master
    parent: architecture-concepts
    weight: 1126
isTocNested: true
showAsideToc: true
---

The YB-Master is the keeper of system meta-data/records, such as what tables exist in the system, where their tablets live, what users/roles exist, the permissions associated with them, and so on.

It is also responsible for coordinating background operations (such as load-balancing or initiating re-replication of under-replicated data) and performing a variety of administrative operations such as create/alter/drop of a table.

Note that the YB-Master is highly available as it forms a Raft group with its peers, and it is not in the critical path of IO against user tables.

![master_overview](/images/architecture/master_overview.png)

Here are some of the functions of the YB-Master.

### Coordination of Universe-wide Admin Operations

Examples of such operations are user-issued create/alter/drop table requests, as well as a creating a backup of a table. The YB-Master performs these operations with a guarantee that the operation is propagated to all tablets irrespective of the state of the YB-TServers hosting these tablets. This is essential because a YB-TServer failure while one of these universe-wide operations is in progress cannot affect the outcome of the operation by failing to apply it on some tablets.

### Storage of System Metadata

The master stores system metadata such as the information about all the keyspaces, tables, roles, permissions, and assignment of tablets to YB-TServers. These system records are replicated across the YB-Masters for redundancy using Raft as well. The system metadata is also stored as a DocDB table by the YB-Master(s).

### Authoritative Source of Tablet Assignments to YB-TServers

The YB-Master stores all tablets and the corresponding YB-TServers that currently host them. This map of tablets to the hosting YB-TServers is queried by clients (such as the YQL layer). Applications using the YB smart clients for various languages (such as Cassandra, Redis, or PostgreSQL(beta)) are very efficient in retrieving data. The smart clients query the YB-Master for the tablet to YB-TServer map and cache it. By doing so, the smart clients can talk directly to the correct YB-TServer to serve various queries without incurring additional network hops.

### Background Operations

These operations performed throughout the lifetime of the universe in the background without impacting foreground read/write performance.

#### Data Placement & Load Balancing

The YB-Master leader does the initial placement (at CREATE table time) of tablets across YB-TServers to enforce any user-defined data placement constraints and ensure uniform load. In addition, during the lifetime of the universe, as nodes are added, fail or
decommissioned, it continues to balance the load and enforce data placement constraints automatically.

#### Leader Balancing

Aside from ensuring that the number of tablets served by each YB-TServer is balanced across the universe, the YB-Masters also ensures that each node has a symmetric number of tablet-peer leaders across eligible nodes.

#### Re-replication of Data on Extended YB-TServer Failure

The YB-Master receives heartbeats from all the YB-TServers, and tracks their liveness. It detects if any YB-TServers has failed, and keeps track of the time interval for which the YB-TServer remains in a failed state. If the time duration of the failure extends beyond a threshold, it finds replacement YB-TServers to which the tablet data of the failed YB-TServer is re-replicated. Re-replication is initiated in a throttled fashion by the YB-Master leader so as to not impact the foreground operations of the universe.


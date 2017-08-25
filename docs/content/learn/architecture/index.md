---
date: 2016-03-09T20:08:11+01:00
title: YugaByte Architecture
weight: 70
---

## About YugaByte

YugaByte is a cloud-native database for mission-critical enterprise applications. It is meant to be
the system-of-record/authoritative database for modern applications. YugaByte allows applications to
easily scale up and scale down in the cloud, on-premise or across hybrid environments without
creating operational complexity or increasing the risk of outages.

In terms of data-model and APIs, YugaByte currently supports **Apache Cassandra CQL** & its
client-drivers natively. In addition to this, it also supports an automatically sharded, clustered &
elastic **Redis-as-a-Database** in a Redis driver compatible manner. Additionally, we are currently
working on **distributed transactions** to support **strongly consistent secondary indexes**,
multi-table/row ACID operations and in general ANSI SQL support.

[TODO: arch diagram]

All of the above data models are powered by a **common underlying core** (a.k.a **YBase**) - a highly
available, distributed system with strongly-consistent write operations, tunable read-consistency
operations, and an advanced log-structured row/documented oriented storage model with several
optimizations for handling ever growing datasets efficiently. Only the upper/edge layer (the
YugaByte Query Layer or **YQL**) of the servers have the API specific aspects - for example, the
server-side implementation of Apache Cassandra and Redis protocols, and the corresponding
query/command compilation and run-time (data type representations, built-in operations, etc.).

In terms of the traditional [CAP theorem](https://en.wikipedia.org/wiki/CAP_theorem), YugaByte is a 
CP database (consistent and partition tolerant), but achieves very high availability. The 
architectural design of YugaByte is similar to Spanner, which is also technically a CP system. 
The description about Spanner
[here](https://cloudplatform.googleblog.com/2017/02/inside-Cloud-Spanner-and-the-CAP-Theorem.html) 
is just as valid for YugaByte. The point is that no system provides 100% availability, so the 
pragmatic question is whether or not the system delivers availability that is so high that most 
users don't worry about its outages. For example, given there are many sources of outages for an 
application, if YugaByte is an insignificant contributor to its downtime, then users are correct to 
not worry about it.

## Overview

A YugaByte cluster, also referred to as a universe, is a group of nodes (VMs, physical machines or
containers) that collectively function as a highly available and resilient database.

A YugaByte universe can be deployed in a variety of configurations depending on business
requirements, and latency considerations. Some examples:

- Single availability zone (AZ/rack/failure domain)
- Multiple AZs in a region
- Multiple regions (with synchronous and asynchronous replication choices)

See here [TODO] for a more elaborate discussion on these deployment options.

A YugaByte *universe* can consist of one or more keyspaces (a.k.a databases in other databases such as
MySQL or Postgres). A keyspace is essentially a namespace and can contain one or more tables.
YugaByte automatically shards, replicates and load-balances these tables across the nodes in the
universe, while respecting user-intent such as cross-AZ or region placement requirements, desired
replication factor, and so on. YugaByte automatically handles failures (e.g., node, AZ or region
failures), and re-distributes and re-replicates data back to desired levels across the remaining
available nodes while still respecting any data placement requirements.

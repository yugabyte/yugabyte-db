---
title: Design Goals
linkTitle: Design Goals
description: Design Goals
aliases:
  - /latest/architecture/design-goals/
menu:
  latest:
    identifier: architecture-design-goals
    parent: architecture
    weight: 1105
isTocNested: false
showAsideToc: true
---

## Consistency

YugaByte DB offers strong consistency guarantees guarantees in the face of a variety of failures. It  supports distributed transactions.

### CAP Theorem

In terms of the [CAP theorem](https://en.wikipedia.org/wiki/CAP_theorem), YugaByte DB is a CP database (consistent and partition tolerant), but achieves very high availability.

The architectural design of YugaByte is similar to Google Cloud Spanner, which is also a CP system. The description about [Spanner](https://cloudplatform.googleblog.com/2017/02/inside-Cloud-Spanner-and-the-CAP-Theorem.html) is just as valid for YugaByte DB. The key takeaway is that no system provides 100% availability, so the pragmatic question is whether or not the system delivers availability that is so high that most users no longer have to be concerned about outages. For example, given there are many sources of outages for an application, if YugaByte DB is an insignificant contributor to its downtime, then users are correct to not worry about it.

### Single-key lineazibility

Linearizability is one of the strongest single-key consistency models, and implies that every operation appears to take place atomically and in some total linear order that is consistent with the real-time ordering of those operations. In other words, the following should be true of operations on a single key:
Operations can execute concurrently, but the state of the database at any point in time must appear to be the result of some totally ordered, sequential execution of operations.
If operation A completes before operation B begins, then B should logically take effect after A.


### Multi-key ACID transactions

YugaByte DB supports multi-key transactions with Snapshot Isolation, note that the Serializable Isolation level is nearing completion as of this writing (Feb 2019).


## Query APIs

YugaByte DB does not re-invent storage APIs. It is wire-compatible with existing APIs and extends functionality. It supports the following APIs:

* **YSQL** which is wire-compatible with PostgreSQL and intends to be ANSI-SQL compliant and is
* **YugaByte Cloud Query Language** or **YCQL** which is a semi-relational API with Cassandra roots


### Distributed SQL

The YSQL API is PostgreSQL compatible as noted before. It re-uses PostgreSQL code base.

* New changes do not break existing PostgreSQL functionality

* Designed with migrations to newer PostgreSQL versions over time as an explicit goal. This means that new features are implemented in a modular fashion in the YugaByte DB codebase to enable rapidly integrating with new PostgreSQL features in an on-going fashion.

* Support wide SQL functionality:
  * All data types
  * Built-in functions and expressions
  * Various kinds of joins
  * Constraints (primary key, foreign key, unique, not null, check)
  * Secondary indexes (incl. multi-column & covering columns)
  * Distributed transactions (Serializable and Snapshot Isolation)
  * Views
  * Stored Procedures
  * Triggers

## Performance

Written ground-up in C++ to ensure high performance and the ability to leverage large memory heaps (RAM) as an internal database cache. It is optimized primarily to run on SSDs and NVMe drives. It is designed with the following workloads in mind:

* High write throughput
* High client concurrency
* High data density (total data set size per node)
* Ability to handle ever growing event data use-cases well



## Geo-Distributed

* Make drivers across the various languages aware of the cluster nodes.

* A single YugaByte DB cluster should be deployable across multiple clouds (both public and private clouds).

* Topology-aware drivers.

* Read-replicas

## Cloud-Native

YugaByte DB is a cloud-native database. It has been designed with the following cloud-native principles in mind:

### Multi-Cloud

* Should run on any public cloud or on-premise datacenter. This means YugaByte DB should be able to run on commodity hardware on bare metal machines, VMs or containers.

* No hard external dependencies. For example, YugaByte DB should not rely on atomic clocks, but should be able to utilize one if available.

* Support multi—zone and geographically replicated deployments

* Work natively in Kubernetes environments as a stateful application.

* YugaByte DB is open source under the very permissive Apache 2.0 license.


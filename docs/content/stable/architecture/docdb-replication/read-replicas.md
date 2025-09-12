---
title: Read replicas in YugabyteDB
headerTitle: Read replicas
linkTitle: Read replicas
description: Learn about read replicas in YugabyteDB.
headContent: Replicate data asynchronously to one or more read replica clusters
menu:
  preview:
    identifier: architecture-docdb-replication-read-replicas
    parent: architecture-docdb-replication
    weight: 400
type: docs
---

In addition to the core distributed consensus-based replication, DocDB extends Raft to add read replicas (also known as observer nodes) that do not participate in writes but get a timeline consistent copy of the data in an asynchronous manner.

Read replicas are a read-only extension to the primary data in the universe. With read replicas, the primary data of the universe is replicated across multiple zones in one region, or across nearby regions. Read replicas do not add to the write latencies as the write does not synchronously replicate data to them. Instead, the data is replicated to read replicas asynchronously.

Nodes in remote data centers can be added in read-only mode. This is typically used in cases where latency of doing a distributed consensus-based write is not tolerable for some workloads.

## Replication factor

Every YugabyteDB universe contains a primary data cluster, and one or more read replica clusters. Thus, each read replica cluster can independently have its own replication factor.

The replication factor of a read replica cluster can be an even number as well. For example, a read replica cluster with a replication factor of 2 is perfectly valid. This is the case because read replicas do not participate in Raft consensus operation, and therefore an odd number of replicas is not required for correctness.

## Writing to read replicas

An application can send write requests to read replicas, but these write requests are internally redirected to the source of truth. This is possible because the read replicas are aware of the topology of the universe.

## Schema changes

Because read replicas are a Raft replication-level extension, the schema changes are transparently applied to these replicas. There is no need to execute DDL operations separately on the read replica cluster.

## Read replicas vs. eventual consistency

The read-only node (or timeline-consistent node) is still strictly better than eventual consistency, because with the latter the application's view of the data can move back and forth in time and is hard to program.

## Learn more

- [Setup read replicas](../../../explore/multi-region-deployments/read-replicas-ysql)

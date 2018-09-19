---
title: MongoDB
linkTitle: MongoDB
description: MongoDB
aliases:
  - /comparisons/mongodb/
menu:
  v1.0:
    parent: comparisons
    weight: 1090
---

Following are the key areas of differences between YugaByte DB and [MongoDB](https://www.mongodb.com/).

## 1. Data Consistency

As documented in the original [Jepsen](https://aphyr.com/posts/322-call-me-maybe-mongodb-stale-reads) analysis, MongoDB has traditionally been a database that can lose data under failures. Starting v3.2, MongoDB switched to Raft-based distributed consensus for [leader election in a replica set](https://docs.mongodb.com/manual/replication/#automatic-failover). While this switch may reduce the time required to start accepting writes after failures, primary data replication necessary to tolerate failures continues to remain [asynchronous](https://docs.mongodb.com/manual/replication/#asynchronous-replication). In other words, MongoDB continues to remain an eventually consistent database (AP as per CAP theorem) and hence continues to suffer all the data loss issues found in such databases.

One such issue is dirty reads. Systems with an eventually consistent core offer weaker data correctness guarantees than timeline-consistent async replication and hence lead to [dirty reads](https://blog.meteor.com/mongodb-queries-dont-always-return-all-matching-documents-654b6594a827) where data read does not match what's actually stored in the database before the read started. To work around this, many MongoDB deployments use *majority read concern* and *majority write concern* (similar to quorum reads and writes in Apache Cassandra). However, these approaches do not offer clean rollback semantics on write failures and hence can still lead to dirty reads.

On the other hand, YugaByte DB is a strongly consistent system (CP as per CAP theorem) built for zero data loss and high data integrity use cases. Primary data replication is synchronous and based on Raft-based distributed consensus. Leader election is also based on Raft. Such a design ensures strong consistency on writes, fast failovers while providing multiple tunable consistency options for reads. YugaByte DB's Enterprise Edition even supports timeline-consistent async replicas for use cases such as OLAP that want to keep a clean copy of the data for read-only purposes in remote datacenters. Synchronous replication of primary data remains in nearby datacenters, thus leading to low latency writes.

## 2. High Read Penalty of Eventual Consistency

In eventually consistent systems, anti-entropy, read-repairs, etc. hurt performance and increase cost. But even in steady state, read operations read from a quorum to serve closer to correct responses and fix inconsistencies. For a replication factor of the 3, such a system’s read throughput is essentially ⅓.

With YugaByte, strongly consistent reads (from leaders), as well as timeline consistent/async reads from followers perform the read operation on only 1 node (and not 3).

## 3. Data Models

MongoDB offers a JSON (aka document-oriented) data model. YugaByte DB currently offers Apache Cassandra & Redis compatible data model with PostgreSQL support in the works. Native JSON support is on the roadmap with an approach similar to how other databases including MySQL, PostgreSQL & Apache Cassandra have added such support.

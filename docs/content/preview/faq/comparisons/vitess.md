---
title: Compare Vitess with YugabyteDB
headerTitle: Vitess
linkTitle: Vitess
description: Compare Vitess with the YugabyteDB database.
aliases:
  - /comparisons/vitess/
menu:
  preview_faq:
    parent: comparisons
    identifier: comparisons-vitess
    weight: 1077
type: docs
---

Vitess is an automated sharding solution for MySQL. Each MySQL instance acts as a shard of the overall database. It uses etcd, a separate strongly consistent key-value store, to store shard location metadata such as which shard is located in which instance. Vitess uses a pool of stateless servers to route queries to the appropriate shard based on the mapping stored in etcd. Each instance uses standard MySQL source-replica replication to account for high availability.

## Single logical SQL database with caveats

While Vitess presents a single logical SQL database to clients, it does not support distributed ACID transactions across shards. Application developers have to be aware of their sharding mechanism and account for it while designing their schema, as well as while executing queries as scalability may be impacted by queries which scatter across shards.

## Lack of continuous availability

Vitess does not make any enhancements to the asynchronous master-slave replication architecture of MySQL. For every shard in the Vitess cluster, another slave instance has to be created and replication has to be maintained. The end result is that Vitess cannot guarantee continuous availability during failures. Spanner-inspired distributed SQL databases like YugabyteDB solve this replication problem at the core using Raft distributed consensus at a per-shard level for both data replication and leader election.

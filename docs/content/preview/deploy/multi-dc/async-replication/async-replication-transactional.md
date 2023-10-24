---
title: Deploy to two universes with transactional xCluster replication
headerTitle: Transactional xCluster deployment
linkTitle: Transactional xCluster
description: Enable deployment using transactional (active-standby) replication between universes
headContent: Transactional (active-standby) replication
menu:
  preview:
    parent: async-replication
    identifier: async-replication-transactional
    weight: 20
type: docs
---

A transactional xCluster deployment preserves and guarantees transactional atomicity and global ordering when propagating change data from one universe to another, as follows:

- Transactional atomicity guarantee. A transaction spanning tablets A and B will either be fully readable or not readable at all on the target universe. A and B can be tablets in the same table OR a table and an index OR different tables.

- Global ordering guarantee. Transactions are visible on the target side in the order they were committed on source.

Due to the asynchronous nature of xCluster replication, this deployment comes with non-zero recovery point objective (RPO) in the case of a source universe outage. The actual value depends on the replication lag, which in turn depends on the network characteristics between the regions.

The recovery time objective (RTO) is very low, as it only depends on the applications switching their connections from one universe to another. Applications should be designed in such a way that the switch happens as quickly as possible.

Transactional xCluster support further allows for the role of each universe to switch during planned and unplanned failover scenarios.

The xCluster role is a property with values ACTIVE or STANDBY that determines and identifies the active (source) and standby (target) universes:

- ACTIVE: The active universe serves both reads & writes. Reads/writes happen as of the latest time and according to the chosen isolation levels.
- STANDBY: The standby universe is meant for reads only. Reads happen as of xCluster safe time for the given database.

xCluster safe time is the transactionally consistent time across all tables in a given database at which Reads are served. In the following illustration, T1 is a transactionally consistent time across all tables.

![Transactional xCluster](/images/deploy/xcluster/xcluster-transactional.png)

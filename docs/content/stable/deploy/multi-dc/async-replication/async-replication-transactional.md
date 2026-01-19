---
title: Deploy transactional xCluster replication
headerTitle: Transactional xCluster
linkTitle: Transactional
description: Deploy using transactional xCluster replication between universes
headContent: Deploy transactional xCluster replication
aliases:
  - /stable/deploy/multi-dc/async-replication-transactional/
  - /stable/deploy/multi-dc/async-replication/async-transactional-setup/
menu:
  stable:
    parent: async-replication
    identifier: async-replication-transactional
    weight: 10
tags:
  other: ysql
type: docs
---

A transactional xCluster deployment preserves and guarantees transactional atomicity and global ordering when propagating change data from one universe to another, as follows:

- Transactional atomicity guarantee. A transaction spanning tablets A and B will either be fully readable or not readable at all on the target universe. A and B can be tablets in the same table OR a table and an index OR different tables.

- Global ordering guarantee. Transactions are visible on the target side in the order they were committed on source.

Transactional xCluster provides the capability to:

- [Switchover](../async-transactional-switchover/): Change the direction of replication without any loss of data. The Primary and Standby universes are swapped gracefully. Applications that require write access to the data should switch their connections to the new Primary universe.

- [Failover](../async-transactional-failover/): Break the replication link and promote the Standby universe in the case of a Primary universe outage. Due to the asynchronous nature of xCluster replication, this comes with non-zero recovery point objective (RPO). The actual value depends on the replication lag, which in turn depends on the network characteristics between the data centers.

The recovery time objective (RTO) is very low, as it only depends on metadata operations to make the Standby transactionally consistent and the applications switching their connections from one universe to another. Applications should be designed in such a way that the switch happens as quickly as possible.

xCluster safe time is the transactionally consistent time across all tables in a given database at which reads are served. In the following illustration, T1 is a transactionally consistent time across all tables.

![Transactional xCluster](/images/deploy/xcluster/xcluster-transactional.png)

## Setup

Transactional xCluster can be set up in the following ways:

- [Automatic mode](../async-transactional-setup-automatic/) Handles all aspects of replication for both data and schema changes.
- [Semi-automatic mode](../async-transactional-setup-semi-automatic/): Compared to manual mode, provides operationally simpler setup and management of replication, as well as fewer steps for performing DDL changes.
- [Fully Manual mode](../async-transactional-setup-manual/): Deprecated.

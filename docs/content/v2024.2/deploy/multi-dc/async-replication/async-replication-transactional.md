---
title: Deploy to two universes with transactional xCluster replication
headerTitle: Transactional xCluster
linkTitle: Transactional
description: Deploy using transactional (active-standby) replication between universes
headContent: Deploy transactional (active-standby) replication
menu:
  v2024.2:
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

xCluster safe time is the transactionally consistent time across all tables in a given database at which Reads are served. In the following illustration, T1 is a transactionally consistent time across all tables.

![Transactional xCluster](/images/deploy/xcluster/xcluster-transactional.png)

## Setup

Transactional xCluster can be in the following ways, depending on the version of YugabyteDB you are deploying.

For v2024.2.0 and later, use Semi-automatic mode.

| Mode | Description | GA | Deprecated |
| :--- | :--- | :--- | :--- |
| Automatic | Handles all aspects of replication for both data and schema changes.<br>Automatic will be available as Early Access in v2025.1. | | |
| [Semi-automatic](../async-transactional-setup-semi-automatic/) | Compared to manual mode, provides operationally simpler setup and management of replication, and fewer steps for performing DDL changes. | v2024.2.0 | v2025.2.1 |
| [Manual](../async-transactional-setup-manual/) | Manual setup and management of replication. DDL changes require manually updating the xCluster configuration.<br>Manual will be deprecated in v2025.1. | v2.18.1 | v2025.1 |

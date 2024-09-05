---
title: Deploy to two universes with transactional xCluster replication
headerTitle: Transactional xCluster deployment
linkTitle: Transactional xCluster
description: Enable deployment using transactional (active-standby) replication between universes
headContent: Transactional (active-standby) replication
menu:
  stable:
    parent: async-replication
    identifier: async-replication-transactional
    weight: 20
badges: ysql
type: docs
---

A transactional xCluster deployment preserves and guarantees transactional atomicity and global ordering when propagating change data from one universe to another, as follows:

- Transactional atomicity guarantee. A transaction spanning tablets A and B will either be fully readable or not readable at all on the target universe. A and B can be tablets in the same table OR a table and an index OR different tables.

- Global ordering guarantee. Transactions are visible on the target side in the order they were committed on source.

Due to the asynchronous nature of xCluster replication, this deployment comes with non-zero recovery point objective (RPO) in the case of a source universe outage. The actual value depends on the replication lag, which in turn depends on the network characteristics between the regions.

The recovery time objective (RTO) is very low, as it only depends on the applications switching their connections from one universe to another. Applications should be designed in such a way that the switch happens as quickly as possible.

Transactional xCluster support further allows for the role of each universe to switch during planned and unplanned failover scenarios.

xCluster safe time is the transactionally consistent time across all tables in a given database at which Reads are served. In the following illustration, T1 is a transactionally consistent time across all tables.

![Transactional xCluster](/images/deploy/xcluster/xcluster-transactional.png)

## Setup

Transactional xCluster can be set up in the following ways:

- [Manual mode](../async-transactional-setup/).
- [Semi-automatic mode](../async-transactional-setup-dblevel/), providing operationally simpler setup and management of replication, as well as simpler steps for performing DDL changes.

## Limitations

- Supports only Active-Standby setups with transactional atomicity and global ordering.
- Transactional consistency is currently not supported for YCQL, only for YSQL.

## Best practices

- Keep CPU use below 65%.
- Keep disk space use under 65%.

## Prerequisites

- Create Primary and Standby universes with TLS enabled.

- Set the YB-TServer [log_min_seconds_to_retain](../../../../reference/configuration/yb-tserver/#log-min-seconds-to-retain) to 86400 on both Primary and Standby.

    This flag determines the duration for which WAL is retained on the Primary universe in case of a network partition or a complete outage of the Standby universe. Be sure to allocate enough disk space to hold WAL generated for this duration.

    The value depends on how long a network partition or standby cluster outage can be tolerated, and the amount of WAL expected to be generated during that period.

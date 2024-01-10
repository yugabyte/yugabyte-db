---
title: Set up transactional xCluster replication
headerTitle: Set up transactional xCluster replication
linkTitle: Set up replication
description: Setting up transactional (active-standby) replication between universes
headContent: Set up unidirectional transactional replication
menu:
  stable_yugabyte-platform:
    parent: disaster-recovery
    identifier: disaster-recovery-setup
    weight: 10
type: docs
---

The following assumes you have set up Primary and Standby universes. Refer to [Set up universes](../async-deployment/#set-up-universes).

## Set up disaster recovery

To set up disaster recovery for a universe, do the following:

1. Navigate to your primary universe and select **Disaster Recovery**.

1. Click **Configure & Enable DR**.

1. Select the DR replica universe, then click **Next: Select Databases**.

1. Select the databases to be copied to the DR replica for disaster recovery.

    You can add databases containing colocated tables to the DR configuration as long as the underlying database is v2.18.1.0 or later. Colocated tables on the DR primary and replica should be created with the same colocation ID if they already exist on both the DR primary and replica prior to DR setup.

1. Select a storage config to be used for backup and restore in case a full copy needs to be transferred to the DR replica database.

## Monitoring replication

After DR is set up, the **Disaster Recovery** tab displays the DR status.

In addition, the **Disaster Recovery** tab provides the following metrics on the **Metrics** tab:

- Async Replication Lag
- Consumer Safe Time Lag
- Consumer Safe Time Skew

The **Disaster Recovery** tab also lists all the tables in replication and their status on the **Tables** tab.

## Set up replication lage alerts



## Adding a database to an existing replication

Note that, although you don't need to create objects on DR replica during initial replication, when you add a new database to an existing replication stream, you _do_ need to create the same objects on the DR replica. If DR primary and replica objects don't match, you won't be able to add the database to the replication.

In addition, If the WALs are garbage collected from DR primary, then the database will need to be bootstrapped. The bootstrap process is handled by YBA. Only the single database is bootstrapped, not all the databases involved in the existing replication.

## Restart replication

In some cases like extended network partitions between the DR primary and replica can cause a permanent failure of replication due to WAL logs being no longer available on the DR primary.

In these cases, restart replication as follows:

1. Navigate to the universe **Disaster Recovery** tab.

1. Click **Actions** and choose **Advanced** and **Restart Replication**.

This performs a full copy of the databases involved from the DR primary to the DR replica.

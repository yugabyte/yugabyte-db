---
title: Set up transactional xCluster replication
headerTitle: Set up Disaster Recovery
linkTitle: Setup
description: Setting up Disaster Recovery for a universe
headContent: Configure Disaster Recovery in case of an outage
menu:
  stable_yugabyte-platform:
    parent: disaster-recovery
    identifier: disaster-recovery-setup
    weight: 10
type: docs
---

The following assumes you have set up DR primary and DR replica. Refer to [Prerequisites](../#prerequisites).

## Set up disaster recovery

To set up disaster recovery for a universe, do the following:

1. Navigate to your primary universe and select **Disaster Recovery**.

1. Click **Configure & Enable DR**.

1. Select the DR replica universe, then click **Next: Select Databases**.

1. Select the databases to be copied to the DR replica for disaster recovery.

    You can add databases containing colocated tables to the DR configuration as long as the underlying database is v2.18.1.0 or later. Colocated tables on the DR primary and replica should be created with the same colocation ID if they already exist on both the DR primary and replica prior to DR setup.

1. Select a storage config to be used for backup and restore in case a full copy needs to be transferred to the DR replica database.

1. Click **Apply Changes**.

## Monitor replication

After DR is set up, the **Disaster Recovery** tab displays the DR status.

### Metrics

In addition, you can monitor the following metrics on the **Metrics** tab:

- Async Replication Lag

    The time in microseconds for the replication lag on the DR replica.

- Consumer Safe Time Lag

    The time elapsed in microseconds between the physical time and safe time. Safe time is when data has been replicated to all the tablets on the DR replica. This metric is available only on the DR replica.

- Consumer Safe Time Skew

    The time elapsed in microseconds for replication between the first and the last tablet replica on the DR replica. This metric is available only on the DR replica.

### Tables

The **Disaster Recovery** tab also lists all the tables in replication and their status on the **Tables** tab.

- To find out the replication lag for a specific table, click the graph icon corresponding to that table.

- To check if the replication has been properly configured for a table, check the status. If properly configured, the table's status is shown as _Operational_.

    The status will be _Not Reported_ momentarily after the replication configuration is created until the source universe reports the replication lag for that table and Prometheus can successfully retrieve it. This should take about 10 seconds.

    If the replication lag increases beyond maximum acceptable lag defined during the replication setup or the lag is not being reported, the table's status is shown as _Warning_.

    If the replication lag has increased to the extent that the replication cannot continue without bootstrapping of current data, the status is shown as _Error: the table's replication stream is broken_, and restarting the replication is required for those tables. If a lag alert is enabled on the replication, you are notified when the lag is behind the specified limit, in which case you may open the table on the replication view to check if any of these tables have their replication status as Error.

    If YugabyteDB Anywhere is unable to obtain the status (for example, due to a heavy workload being run on the universe) the status for that table will be _UnableToFetch_. You may refresh the page to retry gathering information.

- To delete a table from the replication, click **... > Remove Table**. This removes both the table and its index tables from replication. If you decide to remove an index table from the replication group, it does not remove its main table from the replication group.

## Set up replication lag alerts



## Add a database to an existing replication

Note that, although you don't need to create objects on DR replica during initial replication, when you add a new database to an existing replication stream, you _do_ need to create the same objects on the DR replica. If DR primary and replica objects don't match, you won't be able to add the database to the replication.

In addition, If the WALs are garbage collected from DR primary, then the database will need to be bootstrapped. The bootstrap process is handled by YBA. Only the single database is bootstrapped, not all the databases involved in the existing replication.

## Change the DR replica

You can assign a different universe to act as the DR replica.

To change the universe that is used as a DR replica, do the following:

1. Navigate to your primary universe and select **Disaster Recovery**.

1. Click **Actions** and choose **Change DR Replica Universe**.

1. Enter the name of the DR replica and click **Next: Configure Full Copy**.

1. Select a storage config to be used for backup and restore in case a full copy needs to be transferred to the new DR replica.

1. Click **Apply Changes**.

    This removes the current DR replica from the DR config and sets up the new DR replica, with a full copy of the databases if needed.

## Restart replication

In some cases like extended network partitions between the DR primary and replica can cause a permanent failure of replication due to WAL logs being no longer available on the DR primary.

In these cases, restart replication as follows:

1. Navigate to the universe **Disaster Recovery** tab.

1. Click **Actions** and choose **Advanced** and **Restart Replication**.

This performs a full copy of the databases involved from the DR primary to the DR replica.

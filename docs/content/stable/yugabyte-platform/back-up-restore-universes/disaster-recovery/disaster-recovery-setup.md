---
title: Set up transactional xCluster replication
headerTitle: Set up Disaster Recovery
linkTitle: Setup
description: Setting up Disaster Recovery for a universe
headContent: Start replication from your primary to your replica
menu:
  stable_yugabyte-platform:
    parent: disaster-recovery
    identifier: disaster-recovery-setup
    weight: 10
type: docs
---

## Prerequisites

Create two universes, the primary universe which will serve reads and writes, and the DR replica.

Ensure the universes have the following characteristics:

- Both universes have the same encryption in transit settings.
- They can be backed up and restored using the same backup configuration.
- They have enough disk space. DR requires more disk space to store write ahead logs (WAL) in case of a network partition or a temporary outage of the DR replica universe.

Prepare your database and tables on the DR primary. The DR primary can be empty or have data. If the DR primary has a lot of data, the DR setup will take longer because the data must be copied in full to the DR replica before on-going asynchronous replication starts.

On the DR replica, create a database with the same name as that on the DR primary. During initial DR setup, you don't need to create objects on the DR replica. DR performs a full copy of the data to be replicated on the DR primary and automatically creates tables and objects and restores data on the DR replica from the DR primary.

After DR is configured, the DR replica will only be available for reads.

### Best practices

- Monitor CPU and keep its  use below 65%.
- Monitor disk space and keep its use under 65%.
- Create the DR primary and DR replica universes with TLS enabled.
- Set the YB-TServer [log_min_seconds_to_retain](../../../../reference/configuration/yb-tserver/#log-min-seconds-to-retain) flag to 86400 on both DR primary and replica.

    This flag determines the duration for which WAL is retained on the DR primary in case of a network partition or a complete outage of the DR replica. Be sure to allocate enough disk space to hold WAL generated for this duration.

    The value depends on how long a network partition or DR replica outage can be tolerated, and the amount of WAL expected to be generated during that period.

- [Set a replication lag alert](#set-up-replication-lag-alerts) for the DR primary to be alerted when the replication lag exceeds acceptable levels.
- Add new tables and databases to the DR configuration soon after creating them, and before performing any writes to avoid the overhead of a full copy.

## Set up disaster recovery

To set up disaster recovery for a universe, do the following:

1. Navigate to your primary universe and select **Disaster Recovery**.

1. Click **Configure & Enable Disaster Recovery**.

1. Select the universe to use as the DR replica, then click **Next: Select Databases**.

1. Select the databases to be copied to the DR replica for disaster recovery.

    You can add databases containing colocated tables to the DR configuration as long as the underlying database is v2.18.1.0 or later. Colocated tables on the DR primary and replica should be created with the same colocation ID if they already exist on both the DR primary and replica prior to DR setup. Refer to [xCluster and colocation](../../../../architecture/docdb-sharding/colocated-tables/#xcluster-and-colocation).

1. Click **Validate Selection**.

    YugabyteDB Anywhere checks whether or not data needs to be copied to the DR replica for the selected databases and its tables.

1. If data needs to be copied, click **Next: Configure Full Copy**, and select a storage configuration.

    The storage is used to transfer the data to the DR replica database. For information on how to configure storage, see [Configure backup storage](../../configure-backup-storage/).

1. Click **Next: Confirm Alert Threshold**.

    If you have [set an alert for replication lag](#set-up-replication-lag-alerts) on the universe, the threshold for alerting is displayed.

1. Click **Confirm and Enable Disaster Recovery**.

YugabyteDB Anywhere proceeds to set up DR for the universe. How long this takes depends mainly on the amount of data that needs to be copied to the DR replica.

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

The following diagram illustrates these concepts.

![Disaster recovery](/images/deploy/xcluster/xcluster-transactional.png)

The diagram shows that although 3 writes have occurred on cluster A, and 2 of the 3 have been replicated, SQL reads on Cluster B only see 1 of the writes. They only see the first write because it's the only safe, consistent way to read the database.

There are therefore several different key concepts:: replication lag, safe time, and safe time lag:

- Replication lag is the network lag between any two communicating nodes. For example, Repl_Lag(T1) = 10 ms, Repl_Lag(T2) = 100 ms, Repl_Lag(T3) = 20 ms.
- Safe time is the most recent time at which an SQL read against cluster B will be consistent. For example, 3:05:01 on 1/1/2024. After a failover, B will be restored to its state as of the safe time.
- Safe time lag is difference between the current time and the safe time. In the example, the safe time lag is 100 msec.

### Tables

The **Disaster Recovery** tab also lists all the tables in replication and their status on the **Tables** tab.

- To find out the replication lag for a specific table, click the graph icon corresponding to that table.

- To check if the replication has been properly configured for a table, check the status. If properly configured, the table's status is shown as _Operational_.

    The status will be _Not Reported_ momentarily after the replication configuration is created until metrics are available for the replication configuration. This should take about 10 seconds.

    If the replication lag increases beyond maximum acceptable lag defined during the replication setup or the lag is not being reported, the table's status is shown as _Warning_.

    If the replication lag has increased so much that resuming or continuing replication cannot be accomplished via WAL logs but instead requires making another full copy from primary to replica, the status is shown as _Error: the table's replication stream is broken_, and restarting the replication is required for those tables. If a lag alert is enabled on the replication, you are notified when the lag is behind the specified limit, in which case you may open the table on the replication view to check if any of these tables have their replication status as Error.

    If YugabyteDB Anywhere is unable to obtain the status (for example, due to a heavy workload being run on the universe) the status for that table will be _UnableToFetch_. You may refresh the page to retry gathering information.

- To delete a table from the replication, click **... > Remove Table**. This removes both the table and its index tables from replication. If you decide to remove an index table from the replication group, it does not remove its main table from the replication group.

## Set up replication lag alerts

Replication lag measures how far behind in time the DR replica lags the DR primary. In a failover scenario, the longer the lag, the more data is at risk of being lost.

To be notified if the lag exceeds a specific threshold so that you can take remedial measures, set a Universe alert for Replication Lag.

To create an alert:

1. Navigate to **Admin > Alert Configurations > Alert Policies**.
1. Click **Create Alert Policy** and choose **Universe Alert**.
1. Set **Policy Template** to **Replication Lag**.
1. Set **Target** to **Selected Universes** and select the DR primary.
1. Set the conditions for the alert.
1. Click **Save** when you are done.

When DR is set up, YugabyteDB automatically creates an alert for _YSQL Tables in DR/xCluster Config Inconsistent With Primary/Source_. This alert fires when tables are added or dropped from DR primary's databases under replication but are not yet added or dropped from the YugabyteDB Anywhere DR configuration.

For more information on alerting in YugabyteDB Anywhere, refer to [Alerts](../../../alerts-monitoring/alert/).

## Add a database to an existing DR

Note that, although you don't need to create objects on the DR replica during initial DR setup, when you add a new database to an existing DR configuration, you _do_ need to create the same objects on the DR replica. If DR primary and replica objects don't match, you won't be able to add the database to DR.

To add a database to DR, do the following:

1. Navigate to your primary universe and select **Disaster Recovery**.

1. Click **Actions > Select Databases and Tables**.

1. Select the databases to be copied to the DR replica for disaster recovery.

    You can add databases containing colocated tables to the DR configuration as long as the underlying database is v2.18.1.0 or later. Colocated tables on the DR primary and replica should be created with the same colocation ID if they already exist on both the DR primary and replica prior to DR setup.

1. Click **Validate Selection**.

    YugabyteDB Anywhere checks whether or not data needs to be copied to the DR replica for the selected databases and its tables.

1. If data needs to be copied, click **Next: Configure Full Copy**, and select a storage configuration.

    The storage is used to transfer the data to the DR replica database. For information on how to configure storage, see [Configure backup storage](../../configure-backup-storage/).

1. Click **Apply Changes**.

YugabyteDB Anywhere proceeds to copy the database to the DR replica. How long this takes depends mainly on the amount of data that needs to be copied.

## Change the DR replica

You can assign a different universe to act as the DR replica.

To change the universe that is used as a DR replica, do the following:

1. Navigate to your primary universe and select **Disaster Recovery**.

1. Click **Actions** and choose **Change DR Replica Universe**.

1. Enter the name of the DR replica and click **Next: Configure Full Copy**.

1. Select a storage configuration to be used for backup and restore in case a full copy needs to be transferred to the new DR replica.

1. Click **Apply Changes**.

    This removes the current DR replica and sets up the new DR replica, with a full copy of the databases if needed.

## Restart replication

Some situations, such as extended network partitions between the DR primary and replica, can cause a permanent failure of replication due to WAL logs being no longer available on the DR primary.

In these cases, restart replication as follows:

1. Navigate to the universe **Disaster Recovery** tab.

1. Click **Actions** and choose **Advanced** and **Restart Replication**.

This performs a full copy of the databases involved from the DR primary to the DR replica.

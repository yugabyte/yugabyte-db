---
title: Set up Disaster Recovery for a universe
headerTitle: Set up xCluster Disaster Recovery
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

To set up or configure Disaster Recovery, you must be a Super Admin or Admin, or have a role with the Manage xCluster permission. For information on roles, refer to [Manage users](../../../administer-yugabyte-platform/anywhere-rbac/).

### Create universes

Create two universes, the DR primary universe which will serve reads and writes, and the DR replica.

Ensure the universes have the following characteristics:

- Both universes are running the same version of YugabyteDB (v2.18.0.0 or later).
- Both universes have the same [encryption in transit](../../../security/enable-encryption-in-transit/) settings. Encryption in transit is recommended, and you should create the DR primary and DR replica universes with TLS enabled.
- They can be backed up and restored using the same [storage configuration](../../configure-backup-storage/).
- They have enough disk space to support storage of write-ahead logs (WALs) in case of a network partition or a temporary outage of the DR replica universe. During these cases, WALs will continue to write until replication is restored. Consider sizing your disk according to your ability to respond and recover from network or other infrastructure outages.
- DR enables [Point-in-time-recovery](../../pitr/) (PITR) on the DR replica, requiring additional disk space for the replica.

    PITR is used by DR during failover to restore the database to a consistent state. Note that if the DR replica universe already has PITR configured, that configuration is replaced by the DR configuration.

Prepare your database and tables on the DR primary. Make sure the database and tables aren't already being used for xCluster replication; databases and tables can only be used in one replication at a time. The DR primary can be empty or have data. If the DR primary has a lot of data, the DR setup will take longer because the data must be copied in full to the DR replica before on-going asynchronous replication starts.

During DR setup in semi-automatic mode, create objects on the DR replica as well.

DR performs a full copy of the data to be replicated on the DR primary, and restores data on the DR replica from the DR primary.

After DR is configured, the DR replica is only be available for reads.

### Best practices

- Monitor CPU and keep its  use below 65%.
- Monitor disk space and keep its use under 65%.
- Set the YB-TServer [log_min_seconds_to_retain](../../../../reference/configuration/yb-tserver/#log-min-seconds-to-retain) flag to 86400 on both DR primary and replica.

    This flag determines the duration for which WAL is retained on the DR primary in case of a network partition or a complete outage of the DR replica. For xCluster DR, you should set the flag to a value greater than the default. The goal is to retain write-ahead logs (WALs) during a network partition or DR replica outage until replication can be restarted. Setting this to 86400 (24 hours) is a good rule of thumb, but you should also consider how quickly you will be able to recover from a network partition or DR replica outage.

    In addition, during an outage, you will need enough disk space to retain the WALs, so determine the data change rate for your workload, and size your disk accordingly.

- [Set a replication lag alert](#set-up-replication-lag-alerts) for the DR primary to be alerted when the replication lag exceeds acceptable levels.
- Add new tables and databases to the DR configuration soon after creating them, and before performing any writes to avoid the overhead of a full copy.

## Set up disaster recovery

To set up disaster recovery for a universe, do the following:

1. Navigate to your DR primary universe **xCluster Disaster Recovery** tab, and select the replication configuration.

1. Click **Configure & Enable Disaster Recovery** or, if DR has already been set up for a database, **Create Disaster Recovery Config**.

1. Enter a name for the DR configuration.

1. Select the universe to use as the DR replica

1. Click **Next: Select Databases**.

1. Select the databases to be copied to the DR replica for disaster recovery.

    You can add databases containing colocated tables to the DR configuration as long as the underlying database is v2.18.1.0 or later. Colocated tables on the DR primary and replica should be created with the same colocation ID if they already exist on both the DR primary and replica prior to DR setup. Refer to [xCluster and colocation](../../../../explore/colocation/#xcluster-and-colocation).

    YugabyteDB Anywhere checks whether or not data needs to be copied to the DR replica for the selected databases and its tables.

1. If data needs to be copied, click **Next: Confirm Full Copy**, and select a storage configuration.

    The storage is used to transfer the data to the DR replica database. For information on how to configure storage, see [Configure backup storage](../../configure-backup-storage/).

1. Click **Next: Configure PITR Settings**.

    Set the retention period for PITR snapshots.

1. Click **Next: Confirm Alert Threshold**.

    If you have [set an alert for replication lag](#set-up-replication-lag-alerts) on the universe, the threshold for alerting is displayed.

1. Click **Confirm and Enable Disaster Recovery**.

YugabyteDB Anywhere proceeds to set up DR for the universe. How long this takes depends mainly on the amount of data that needs to be copied to the DR replica.

## Monitor replication

After DR is set up, the **xCluster Disaster Recovery** tab displays the DR status.

![Disaster recovery](/images/yb-platform/disaster-recovery/disaster-recovery-status.png)

### Metrics

In addition, you can monitor the following metrics on the **xCluster Disaster Recovery > Metrics** tab:

- Async Replication Lag

    The network lag in microseconds between any two communicating nodes.

    If you have [set an alert for replication lag](#set-up-replication-lag-alerts), you can also display the alert threshold.

- Consumer Safe Time Lag

    The time elapsed in microseconds between the physical time and safe time. Safe time is the time (usually in the past) at which the database or tables can be read with full correctness and consistency. For example, even though the actual time may be 3:00:00, a query or read against the DR replica database may be able to only return a result as of 2:59:59 (that is 1 second ago) due to network lag and/or out-of-order delivery of datagrams.

- Consumer Safe Time Skew

    The time elapsed in microseconds for replication between the most caught up tablet and the tablet that lags the most on the DR replica. This metric is available only on the DR replica.

Consider the following scenario.

![Disaster recovery metrics](/images/yb-platform/disaster-recovery/disaster-recovery-metrics.png)

- Three transactions, T1, T2, and T3, are written to the DR primary at 0.001 ms, 0.002 ms, and 0.003 ms respectively.
- The replication lag for the transactions are as follows: Repl_Lag(T1) = 10 ms, Repl_Lag(T2) = 100 ms, Repl_Lag(T3) = 20 ms.

The state of the system at time t = 50 ms is as follows:

- T1 and T3 have arrived at the DR replica. T2 is still in transit.
- Although T3 has arrived, SQL reads on the DR replica only see T1 (T3 is hidden), because (due to T2 still being in transit, and T3 being written on the DR primary _after_ T2) the only safe, consistent view of the database is to see T1 and hide T3.
- Safe time is the time at which T1 was replicated, namely t = 0.001 ms.
- Safe time lag is the difference between the current time and the safe time. As of t = 50 ms, the safe time lag is 49.999 ms (50 ms - 0.001 ms).

If a failover were to occur at this moment (t = 50 ms) the DR replica will be restored to its state as of the safe time (that is, t = .001 ms), meaning that T1 will be visible, but T3 will be hidden. T2 (which is still in transit) is currently not available on the DR Replica, and will be ignored when it arrives.

In this example, the safe time skew is 90 ms, the difference between Repl_Lag(T1) and Repl_Lag(T3).

### Tables

The **xCluster Disaster Recovery** tab also lists all the tables in replication and their status on the **Tables** tab.

![Disaster recovery](/images/yb-platform/disaster-recovery/disaster-recovery-tables.png)

- To find out the replication lag for a specific table, click the graph icon corresponding to that table.

- To delete a table from the replication, click **... > Remove Table**. This removes both the table and its index tables from replication. If you decide to remove an index table from the replication group, it does not remove its main table from the replication group.

- Use the search bar to filter the view by table name, database, size, and more.

#### Status

To check if the replication has been properly configured for a table, check the status. If properly configured, the table's replication status is shown as _Operational_.

The status will be _Not Reported_ momentarily after the replication configuration is created until metrics are available for the replication configuration. This should take about 10 seconds.

If the replication lag has increased so much that resuming or continuing replication cannot be accomplished via WAL logs but instead requires making another full copy from DR primary to DR replica, the status is shown as _Missing op ID_, and you must [restart replication](#restart-replication) for those tables. If a lag alert is enabled on the replication, you are notified when the lag is behind the [replication lag alert](#set-up-replication-lag-alerts) threshold; if the replication stream is not yet broken and the lag is due to some other issues, the status is shown as _Warning_.

If YugabyteDB Anywhere is unable to obtain the status (for example, due to a heavy workload being run on the universe), the status for that table will be _Unable To Fetch_. You may refresh the page to retry gathering information.

The table statuses are described in the following table.

| Status | Description |
| :--- | :--- |
| In Progress | The table is undergoing changes, such as being added to or removed from replication. |
| Bootstrapping | The table is undergoing a full copy; that is, being backed up from the DR primary and being restored to the DR replica. |
| Validated | The table passes pre-checks and is eligible to be added to replication. |
| Operational | The table is being replicated. |

The following statuses [trigger an alert](#set-up-replication-lag-alerts).

| Status | Description |
| :--- | :--- |
| Failed | The table failed to be added to replication. |
| Warning | The table is in replication, but the replication lag is more than the [maximum acceptable lag](#set-up-replication-lag-alerts), or the lag is not being reported. |
| Dropped From Source | The table was in replication, but dropped from the DR primary without first being [removed from replication](../disaster-recovery-tables/#remove-a-table-from-dr). If you are using Manual mode, you need to remove it manually from the configuration. In Semi-automatic mode, you don't need to remove it manually. |
| Dropped From Target | The table was in replication, but was dropped from the DR replica without first being [removed from replication](../disaster-recovery-tables/#remove-a-table-from-dr). If you are using Manual mode, you need to remove it manually from the configuration. In Semi-automatic mode, you don't need to remove it manually. |
| Extra Table On Source | The table is newly created on the DR primary but is not in replication yet. |
| Extra Table On Target | The table is newly created on the DR replica but it is not in replication yet. |
| Missing op ID | The replication is broken and cannot continue because the write-ahead-logs are garbage collected before they were replicated to the other universe and you will need to [restart replication](#restart-replication).|
| Schema&nbsp;mismatch | The schema was updated on the table (on either of the universes) and replication is paused until the same schema change is made to the other universe. |
| Missing table | For colocated tables, only the parent table is in the replication group; any child table that is part of the colocation will also be replicated. This status is displayed for a parent colocated table if a child table only exists on the DR primary. Create the same table on the DR replica. |
| Auto flag config mismatch | Replication has stopped because one of the universes is running a version of YugabyteDB that is incompatible with the other. This can happen when upgrading universes that are in replication. Upgrade the other universe to the same version. |

### Set up replication lag alerts

Replication lag measures how far behind in time the DR replica lags the DR primary. In a failover scenario, the longer the lag, the more data is at risk of being lost.

To be notified if the lag exceeds a specific threshold so that you can take remedial measures, set a Universe alert for Replication Lag. Note that to display the lag threshold in the [Async Replication Lag chart](#metrics), the alert Severity and Condition must be Severe and Greater Than respectively.

To create an alert:

1. Navigate to **Admin > Alert Configurations > Alert Policies**.
1. Click **Create Alert Policy** and choose **Universe Alert**.
1. Set **Policy Template** to **Replication Lag**.
1. Enter a name and description for the alert policy.
1. Set **Target** to **Selected Universes** and select the DR primary.
1. Set the conditions for the alert.

    - Enter a duration threshold. The alert is triggered when the lag exceeds the threshold for the specified duration.
    - Set the **Severity** option to Severe.
    - Set the **Condition** option to Greater Than.
    - Set the threshold to an acceptable value for your deployment. The default threshold is 3 minutes (180000ms).

1. Click **Save** when you are done.

When DR is set up, YugabyteDB automatically creates the alert _XCluster Config Tables are in bad state_. This alert fires when:

- there is a table schema mismatch between DR primary and replica.
- tables are added or dropped from either DR primary or replica, but have not been added or dropped from the other.

When you receive an alert, navigate to the replication configuration [Tables tab](#tables) to see the table status.

YugabyteDB Anywhere collects these metrics every 2 minutes, and fires the alert within 10 minutes of the error.

For more information on alerting in YugabyteDB Anywhere, refer to [Alerts](../../../alerts-monitoring/alert/).

## Manage replication

### Add a database to an existing DR

On the DR replica, create a database with the same name as that on the DR primary.

- In [Manual mode](../#manual-mode), you don't need to create objects on the DR replica; DR performs a full copy of the data to be replicated on the DR primary, and automatically creates tables and objects, and restores data on the DR replica from the DR primary.
- In [Semi-automatic mode](../#semi-automatic-mode), you need to create all objects (tables, indexes, and so on) on the DR replica exactly as they are on the DR primary _prior_ to setting up xCluster DR.

To add a database to DR, do the following:

1. Navigate to your DR primary universe **xCluster Disaster Recovery** tab and select the replication configuration.

1. Click **Actions > Select Databases and Tables**.

1. Select the databases to be copied to the DR replica for disaster recovery.

    You can add databases containing colocated tables to the DR configuration as long as the underlying database is v2.18.1.0 or later. Colocated tables on the DR primary and replica should be created with the same colocation ID if they already exist on both the DR primary and replica prior to DR setup. Refer to [xCluster and colocation](../../../../explore/colocation/#xcluster-and-colocation).

1. Click **Validate Selection**.

    YugabyteDB Anywhere checks whether or not data needs to be copied to the DR replica for the selected databases and its tables.

1. If data needs to be copied, click **Next: Confirm Full Copy**.

1. Click **Apply Changes**.

YugabyteDB Anywhere proceeds to copy the database to the DR replica. How long this takes depends mainly on the amount of data that needs to be copied.

### Change the DR replica

You can assign a different universe to act as the DR replica.

To change the universe that is used as a DR replica, do the following:

1. Navigate to your DR primary universe **xCluster Disaster Recovery** tab and select the replication configuration.

1. Click **Actions** and choose **Change DR Replica Universe**.

1. Enter the name of the DR replica and click **Next: Confirm Full Copy**.

1. Click **Apply Changes**.

    This removes the current DR replica and sets up the new DR replica, with a full copy of the databases if needed.

### Restart replication

Some situations, such as extended network partitions between the DR primary and replica, can cause a permanent failure of replication due to WAL logs being no longer available on the DR primary.

In these cases, restart replication as follows:

1. Navigate to your DR primary universe **xCluster Disaster Recovery** tab and select the replication configuration.
1. Click **Actions** and choose **Advanced** and **Resync DR Replica**.
1. Select the databases to be copied to the DR replica.
1. Click **Next: Confirm Full Copy**.
1. Click **Create a New Full Copy**.

This performs a full copy of the databases involved from the DR primary to the DR replica.

### Remove disaster recovery

To remove disaster recovery for a universe, do the following:

1. Navigate to your DR primary universe **xCluster Disaster Recovery** tab and select the replication configuration you want to remove.

1. Click **Actions** and choose **Remove Disaster Recovery**.

---
title: Set up Disaster Recovery for a Aeon cluster
headerTitle: Set up Disaster Recovery
linkTitle: Setup
description: Setting up Disaster Recovery for a Aeon cluster
headContent: Start replication from your source to your target
menu:
  stable_yugabyte-cloud:
    parent: disaster-recovery-aeon
    identifier: disaster-recovery-setup-aeon
    weight: 10
type: docs
---

## Prerequisites

To set up or configure Disaster Recovery, you must be an Admin, or have a role with Disaster Recovery permissions. For information on roles, refer to [Manage users](../../../managed-security/managed-roles/).

### Create clusters

Create two clusters, the Source cluster which will serve reads and writes, and the Target.

Ensure the clusters have the following characteristics:

- Both clusters are running the same version of YugabyteDB ({{<release "2025.2">}} or later).
- Both clusters are deployed in a [VPC](../../../cloud-basics/cloud-vpcs/cloud-vpc-intro/) and are on the same cloud provider.
- They have enough disk space to support storage of write-ahead logs (WALs) in case of a network partition or a temporary outage of the Target cluster. During these cases, WALs will continue to write until replication is restored. Consider sizing your disk according to your ability to respond and recover from network or other infrastructure outages.
- DR enables point-in-time-recovery (PITR) on both the Source and the Target. Although PITR is not required on the Source for DR purposes, enabling it reduces the switchover task execution time. This leads to increase storage use for the sake of performance.

    PITR is used by DR during failover to restore the database to a consistent state. Note that if the Target cluster already has PITR configured, that configuration is replaced by the PITR configuration set during DR setup.

Prepare your database and tables on the Source. Make sure the database and tables aren't already being used for xCluster replication; databases and tables can only be used in one replication at a time. The Source can be empty or have data. If the Source has a lot of data, the DR setup will take longer because the data must be copied in full to the Target before on-going asynchronous replication starts.

DR performs a full copy of the data to be replicated on the Source, and restores the data to the Target.

After DR is configured, the Target is only available for reads.

### Best practices

- Monitor CPU and keep its use below 65%.
- Monitor disk space and keep its use under 65%.
- Perform DDL changes on the Target immediately after performing them on Source to avoid the overhead of a full copy.

## Set up disaster recovery

To set up disaster recovery for a cluster, do the following:

1. Navigate to your Source cluster **Disaster Recovery** tab.

1. Click **Configure and Enable Disaster Recovery**.

1. Enter a name for the DR configuration.

1. Select the databases to be copied to the Target for disaster recovery.

    Colocated tables on the Source and Target should be created with the same colocation ID if they already exist on both the Source and Target prior to DR setup. Refer to [xCluster and colocation](../../../../additional-features/colocation/#xcluster-and-colocation).

    YugabyteDB Aeon checks whether or not data needs to be copied to the Target for the selected databases and their tables.

1. Select the target cluster.

1. Click **Enable Disaster Recovery**.

YugabyteDB Aeon proceeds to set up DR for the cluster. How long this takes depends mainly on the amount of data that needs to be copied to the Target.

## Monitor replication

After DR is set up, the **Disaster Recovery** tab displays the DR status, and potential data loss on failover in microseconds.

![Disaster recovery](/images/yb-cloud/dr-replicating.png)

YugabyteDB Aeon also automatically sets up safe time and replication lag [alerts](#disaster-recovery-alerts).

### Metrics

In addition, you can monitor the following metrics on the **Disaster Recovery** tab:

- Monitoring - Safe Time Lag

    The time elapsed in microseconds between the physical time and safe time. Safe time is the time (usually in the past) at which the database or tables can be read with full correctness and consistency. For example, even though the actual time may be 3:00:00, a query or read against the Target database may be able to only return a result as of 2:59:59 (that is 1 second ago) due to network lag and/or out-of-order delivery of datagrams.

- Troubleshooting - Replication Lag

    The network lag in microseconds between any two communicating nodes, and the replication lag alert threshold.

- Consumer Safe Time Skew

    The time elapsed in microseconds for replication between the most caught up tablet and the tablet that lags the most on the Target. This metric is available only on the Target.

Consider the following scenario.

![Disaster recovery metrics](/images/yb-platform/disaster-recovery/disaster-recovery-metrics.png)

- Three transactions, T1, T2, and T3, are written to the Source at 0.001 ms, 0.002 ms, and 0.003 ms respectively.
- The replication lag for the transactions are as follows: Repl_Lag(T1) = 10 ms, Repl_Lag(T2) = 100 ms, Repl_Lag(T3) = 20 ms.

The state of the system at time t = 50 ms is as follows:

- T1 and T3 have arrived at the Target. T2 is still in transit.
- Although T3 has arrived, SQL reads on the Target only see T1 (T3 is hidden), because (due to T2 still being in transit, and T3 being written on the Source _after_ T2) the only safe, consistent view of the database is to see T1 and hide T3.
- Safe time is the time at which T1 was replicated, namely t = 0.001 ms.
- Safe time lag is the difference between the current time and the safe time. As of t = 50 ms, the safe time lag is 49.999 ms (50 ms - 0.001 ms).

If a failover were to occur at this moment (t = 50 ms) the Target will be restored to its state as of the safe time (that is, t = .001 ms), meaning that T1 will be visible, but T3 will be hidden. T2 (which is still in transit) is currently not available on the DR Replica, and will be ignored when it arrives.

In this example, the safe time skew is 90 ms, the difference between Repl_Lag(T1) and Repl_Lag(T2) (the transaction that is lagging the most).

### Disaster recovery alerts

YugabyteDB Aeon sends a notification when lag exceeds the threshold, as follows:

- Safe time lag exceeds 5 minutes (Warning) or 10 minutes (Severe).
- Replication lag exceeds 5 minutes (Warning) or 10 minutes (Severe).

Replication lag measures how far behind in time the Target lags the Source. In a failover scenario, the longer the lag, the more data is at risk of being lost.

Note that to display the lag threshold in the [Async Replication Lag chart](#metrics), the alert Severity and Condition must be Severe and Greater Than respectively.

<!--When DR is set up, YugabyteDB automatically creates the alert _XCluster Config Tables are in bad state_. This alert fires when:

- there is a table schema mismatch between Source and Target.
- tables are added or dropped from either Source or Target, but have not been added or dropped from the other.-->

When you receive an alert, navigate to the Disaster Recovery [Database and Tables](#tables) to see the table status.

YugabyteDB Aeon collects these metrics every 30 seconds, and fires the alert within 3 minutes of the error.

For more information on alerting in YugabyteDB Aeon, refer to [Alerts](../../../cloud-monitor/cloud-alerts/).

### Tables

After disaster recovery is set up and replicating, the **Disaster Recovery** tab lists all the databases and tables in replication and their status under **Database and Tables**.

![Disaster recovery - Databases and Tables](/images/yb-cloud/dr-databases-and-tables.png)

Use the search bar to filter the view by name and status.

#### Status

To check if the replication has been properly configured for a table, check the status. If properly configured, the table's status is shown as _Operational_.

The status will be _Not Reported_ momentarily after the replication configuration is created until metrics are available for the replication configuration. This should take about 10 seconds.

If the replication lag has increased so much that resuming or continuing replication cannot be accomplished via WAL logs but instead requires making another full copy from Source to Target, the status is shown as _Missing op ID_, and you must [restart replication](#restart-replication) for the databases those tables belong to. If a lag alert is enabled on the replication, you are notified when the lag is behind the [replication lag alert](#set-up-replication-lag-alerts) threshold; if the replication stream is not yet broken and the lag is due to some other issues, the status is shown as _Warning_.

If YugabyteDB Aeon is unable to obtain the status (for example, due to a heavy workload being run on the cluster), the status for that table will be _Unable To Fetch_. You may refresh the page to retry gathering information.

The table statuses are described in the following table.

| Status | Description |
| :--- | :--- |
| In Progress | The table is undergoing changes, such as being added to or removed from replication. |
| Bootstrapping | The table is undergoing a full copy; that is, being backed up from the Source and being restored to the Target. |
| Validated | The table passes pre-checks and is eligible to be added to replication. |
| Operational | The table is being replicated. |

The following statuses [trigger an alert](#set-up-replication-lag-alerts).

| Status | Description |
| :--- | :--- |
| Failed | The table failed to be added to replication. |
| Warning | The table is in replication, but the replication lag is more than the [maximum acceptable lag](#set-up-replication-lag-alerts), or the lag is not being reported. |
| Dropped From Source | The table was in replication, but dropped from the Source without first being [removed from replication](../disaster-recovery-tables/#remove-a-table-from-dr). |
| Dropped From Target | The table was in replication, but was dropped from the Target without first being [removed from replication](../disaster-recovery-tables/#remove-a-table-from-dr). |
| Extra Table On Source | The table is newly created on the Source but is not in replication yet. |
| Extra Table On Target | The table is newly created on the Target but it is not in replication yet. |
| Schema&nbsp;mismatch | The schema was updated on the table (on either of the clusters) and replication is paused until the same schema change is made to the other cluster. |
| Missing table | For colocated tables, only the parent table is in the replication group; any child table that is part of the colocation will also be replicated. This status is displayed for a parent colocated table if a child table only exists on the Source. Create the same table on the Target. |
| Unable to Fetch | The replication status is currently not available. Try a again in a bit. If the error continues, contact support. |
| Unknown | The replication status is currently not available. Try a again in a bit. If the error continues, contact support. |

The following statuses describe replication errors. More than one of these errors may be displayed.

| Status | Description |
| :--- | :--- |
| Error | Replication is in an error state, but the error is not known. |
| Missing op ID | The replication is broken and cannot continue because the write-ahead-logs are garbage collected before they were replicated to the other cluster and you will need to [restart replication](#repair-replication).|
| Schema&nbsp;mismatch | The schema was updated on the table (on either of the clusters) and replication is paused until the same schema change is made to the other cluster. |
| Missing table | For colocated tables, only the parent table is in the replication group; any child table that is part of the colocation will also be replicated. This status is displayed for a parent colocated table if a child table only exists on the Source. Create the same table on the Target. |
| Auto flag config mismatch | Replication has stopped because one of the clusters is running a version of YugabyteDB that is incompatible with the other. This can happen when upgrading clusters that are in replication. Upgrade the other cluster to the same version. |

## Manage replication

### Add or remove a database

To add a database to DR, do the following:

1. Navigate to your Source cluster **Disaster Recovery** tab and select the DR configuration.

1. Click **Edit DR Configuration** and choose **Add Database**.

1. Select the databases to be copied to the Target for disaster recovery.

    You can add databases containing colocated tables to the DR configuration. Colocated tables on the Source and Target should be created with the same colocation ID if they already exist on both the Source and Target prior to DR setup. Refer to [xCluster and colocation](../../../../additional-features/colocation/#xcluster-and-colocation).

1. Click **Add Databases**.

YugabyteDB Aeon proceeds to copy the database to the Target. How long this takes depends mainly on the amount of data that needs to be copied.

To remove a database from DR, do the following:

1. Navigate to your Source cluster **Disaster Recovery** tab and select the DR configuration.

1. Click **Edit DR Configuration** and choose **Remove Database**.

1. Select the databases to be removed.

1. Click **Remove Databases**.

### Remove DR

To assign a different cluster to act as the Target, remove the existing DR and create a new configuration using the new Target.

To remove DR, do the following:

1. Navigate to your Source cluster **Disaster Recovery** tab and select the DR configuration.

1. Click **Edit DR Configuration** and choose **Remove DR**.

### Restart replication

Some situations, such as extended network partitions between the Source and Target, can cause a permanent failure of replication due to WAL logs being no longer available on the Source.

In these cases, restart replication by navigating to your Source cluster **Disaster Recovery** tab and click **Restart Replication**.

---
title: Set up xCluster replication for a universe
headerTitle: Set up xCluster replication
linkTitle: Setup
description: Setting up xCluster replication for a universe
headContent: Start replication from your source to your target
menu:
  preview_yugabyte-platform:
    parent: xcluster-replication
    identifier: xcluster-replication-setup
    weight: 10
type: docs
---

## Prerequisites

Create two universes, the source universe which will serve reads and writes, and the target.

Ensure the universes have the following characteristics:

- Both universes are running the same version of YugabyteDB (v2.18.0.0 or later).
- Both universes have the same [encryption in transit](../../../security/enable-encryption-in-transit/) settings. Encryption in transit is recommended, and you should create the source and target universes with TLS enabled.

    If you rotate the CA certificate on the source universe, you need to restart the replication so that the target nodes get the new root certificate for TLS verifications.

- They can be backed up and restored using the same [storage configuration](../../../back-up-restore-universes/configure-backup-storage/).

    The storage configuration for copying data from source to target must be accessible from both universes. For example, if AWS S3 is used as the target of backups, the same S3 bucket must be accessible from the source and target universe.

    If an NFS file server is used, the file server or a mirror of the file server in the target region must be accessible at the same mount point on both universes. If there is significant delay in file transfer between the file server and its mirror in a different region, set the [runtime configuration](../../../administer-yugabyte-platform/manage-runtime-config/) `yb.xcluster.sleep_time_before_restore` to the maximum delay in mirroring files across the regions.

- They have enough disk space to support storage of write-ahead logs (WALs) in case of a network partition or a temporary outage of the target universe. During these cases, WALs will continue to write until replication is restored. Consider sizing your disk according to your ability to respond and recover from network or other infrastructure outages.

    In addition, during xCluster replication setup, the source universe retains WAL logs and increases in size. The setup (including copying data from source to target) is not aborted if the source universe runs out of space. Instead, a warning is displayed notifying that the source universe has less than 100 GB of space remaining. Ensure that there is enough space available on the source universe before attempting to set up the replication. A recommended approach would be to estimate that the available disk space on the source universe is the same as the used disk space.

- Neither universe is already being used for xCluster replication.

Prepare your database and tables on the source. The source can be empty or have data. If the source has a lot of data, setup will take longer because the [data must be copied in full to the target](#full-copy-during-xcluster-setup) before on-going asynchronous replication starts.

On the target, create a database with the same name as that on the source. During initial setup, you don't need to create objects on the target. YugabyteDB Anywhere performs a full copy of the data to be replicated on the source, and automatically creates tables and objects, and restores data on the target from the source.

After replication is configured, by default the target will only be available for reads.

## Best practices

- Monitor CPU use and keep it below 65%.
- Monitor disk space and keep its use under 65%.
- Set the YB-TServer [log_min_seconds_to_retain](../../../../reference/configuration/yb-tserver/#log-min-seconds-to-retain) flag to 86400 on both source and target.

    This flag determines the duration for which WAL is retained on the source in case of a network partition or a complete outage of the target. For xCluster replication, you should set the flag to a value greater than the default. The goal is to retain write-ahead logs (WALs) during a network partition or target outage until replication can be restarted. Setting this to 86400 (24 hours) is a good rule of thumb, but you should also consider how quickly you will be able to recover from a network partition or target outage.

    In addition, during an outage, you will need enough disk space to retain the WALs, so determine the data change rate for your workload, and size your disk accordingly.

- [Set a replication lag alert](#set-up-replication-lag-alerts) for the source to be alerted when the replication lag exceeds acceptable levels.
- Add new tables and databases to the replication configuration soon after creating them, and before performing any writes to avoid the overhead of a full copy.
- If xCluster replication setup clashes with scheduled backups, wait for the scheduled backup to finish, and then restart the replication.

### Full copy during xCluster setup

When xCluster is [set up](#set-up-xcluster-replication) or [restarted](#restart-replication), or when new [tables or databases are added](../xcluster-replication-ddl/) to an existing xCluster configuration, YugabyteDB Anywhere checks if a full copy is required before configuring the replication. A full copy is required in the following circumstances:

- Any newly added databases or tables or index tables are not present on the target universe.
- Any databases or tables or index tables are not empty. Newly created index tables associated with non-empty main tables are considered non-empty and trigger a full copy.

For YSQL, a full copy is performed on the entire database; for YCQL, a full copy of the table (including associated index tables) is performed.

A full copy causes corresponding databases and tables on the target universe to be recreated, and existing data on the target universe in those tables is dropped.

Ideally, you should add tables to replication right after creating them on both universes to avoid unnecessary full copy operations.

A full copy is skipped if the corresponding tables are already in replication on the target universe. Specifically, for bidirectional replication, this means that setting up or adding tables to xCluster replication from universe A to universe B might trigger a full copy; but setting up replication in the reverse direction from B to A afterwards does not trigger a full copy. See [Bidirectional replication](../bidirectional-replication/) for more information.

A full copy is done by first backing up the data to external storage, and then restoring to the target universe.

- For information on creating and using storage configurations, refer to [Configure backup storage](../../../back-up-restore-universes/configure-backup-storage/)
- To configure the performance of backup and restore, refer to [Configure backup performance parameters](../../../back-up-restore-universes/back-up-universe-data/#configure-backup-performance-parameters).

### YSQL tables

Replication is set up at the table level on the source and target universes. Selecting a YSQL database adds all its current tables to the xCluster configuration. If you add any tables that you want replicated, you must manually add them to the xCluster configuration in YugabyteDB Anywhere.

When adding tables or during xCluster setup, however, you must select the database with the tables you want, and all tables in a selected database are added to the replication.

If a full copy is required, the entire database is recreated on the target universe from the current database on the source universe. Be sure to keep the set of tables the same at all times on both the source and target universes in these databases by following the steps in [Manage tables and indexes](../xcluster-replication-ddl/).

To be eligible for xCluster replication, tables must not already be in replication. That is, the table can't already be in another xCluster configuration between the same two universes in the same direction.

If a YSQL database includes tables considered ineligible for replication, this database cannot be selected in the YugabyteDB Anywhere UI and the database as a whole cannot be replicated.

You can add databases containing colocated tables to the xCluster configuration as long as the underlying database is v2.18.1.0 or later. Colocated tables on the source and target should be created with the same colocation ID if they already exist on both the source and target prior to setup. Refer to [xCluster and colocation](../../../../architecture/docdb-sharding/colocated-tables/#xcluster-and-colocation).

## Set up xCluster replication

To set up replication for a universe, do the following:

1. Navigate to your source universe and select **xCluster Replication**.

1. Click **Configure Replication**.

1. Enter a name for the replication.

1. Select the universe to use as the target.

1. Select the API, YSQL or YCQL.

1. If you selected YSQL, choose whether to use [transactional atomicity](../#xcluster-configurations).

1. Click **Next: Select Databases** (YSQL) or **Select Tables** (YCQL).

1. Select the databases or tables to be copied to the target for replication.

    You can add databases containing colocated tables to the replication configuration as long as the underlying database is v2.18.1.0 or later. Colocated tables on the source and target should be created with the same colocation ID if they already exist on both the source and target prior to setup. Refer to [xCluster and colocation](../../../../architecture/docdb-sharding/colocated-tables/#xcluster-and-colocation).

1. Click **Validate Table Selection**.

    YugabyteDB Anywhere checks whether or not data needs to be copied to the target for the selected databases and its tables.

1. If data needs to be copied, click **Next: Configure Full Copy**, and select a storage configuration.

    The storage is used to transfer the data to the target database. For information on how to configure storage, see [Configure backup storage](../../../back-up-restore-universes/configure-backup-storage/).

1. Click **Next: Confirm Alert Threshold**.

    If you have [set an alert for replication lag](#set-up-replication-lag-alerts) on the universe, the threshold for alerting is displayed.

1. Click **Confirm and Enable Replication**.

YugabyteDB Anywhere proceeds to set up replication for the universe. How long this takes depends mainly on the amount of data that needs to be copied to the target. You can view the progress of xCluster tasks by navigating to the universe **Tasks** tab.

## Monitor replication

After replication is set up, the **xCluster Replication** tab displays the status.

![Replication](/images/yb-platform/xcluster/xcluster-status.png)

To display the replication details, click the replication name.

### Metrics

You can monitor the following metrics on the **Metrics** tab:

- Async Replication Lag

    The network lag in microseconds between any two communicating nodes.

    If you have [set an alert for replication lag](#set-up-replication-lag-alerts), you can also display the alert threshold.

YSQL transactional replication displays the following additional metrics:

- Consumer Safe Time Lag

    The time elapsed in microseconds between the physical time and safe time. Safe time is the time (usually in the past) at which the database or tables can be read with full correctness and consistency. For example, even though the actual time may be 3:00:00, a query or read against the target database may be able to only return a result as of 2:59:59 (that is 1 second ago) due to network lag and/or out-of-order delivery of datagrams.

- Consumer Safe Time Skew

    The time elapsed in microseconds for replication between the most caught up tablet and the tablet that lags the most on the target. This metric is available only on the target.

For more information, refer to [Metrics](../../../back-up-restore-universes/disaster-recovery/disaster-recovery-setup/#metrics).

### Tables

The replication details also provide all the tables in replication and their status on the **Tables** tab.

![Replication](/images/yb-platform/xcluster/xcluster-tables.png)

- To find out the replication lag for a specific table, click the graph icon corresponding to that table.

- To check if the replication has been properly configured for a table, check the status. If properly configured, the table's replication status is shown as _Operational_.

    The status will be _Not Reported_ momentarily after the replication configuration is created until metrics are available for the replication configuration. This should take about 10 seconds.

    If the replication lag increases beyond maximum acceptable lag defined during the replication setup or the lag is not being reported, the table's status is shown as _Warning_.

    If the replication lag has increased so much that resuming or continuing replication cannot be accomplished via WAL logs but instead requires making another full copy from source to target, the status is shown as _Error: the table's replication stream is broken_, and [restarting the replication](#restart-replication) is required for those tables. If a lag alert is enabled on the replication, you are notified when the lag is behind the specified limit, in which case you may open the table on the replication view to check if any of these tables have their replication status as Error.

    If YugabyteDB Anywhere is unable to obtain the status (for example, due to a heavy workload being run on the universe) the status for that table will be _UnableToFetch_. You may refresh the page to retry gathering information.

- To delete a table from the replication, click **... > Remove Table**. This removes both the table and its index tables from replication. If you decide to remove an index table from the replication group, it does not remove its main table from the replication group.

## Set up replication lag alerts

Replication lag measures how far behind in time the target lags the source. In a failover scenario, the longer the lag, the more data is at risk of being lost.

To be notified if the lag exceeds a specific threshold so that you can take remedial measures, set a Universe alert for Replication Lag. Note that to display the lag threshold in the [Async Replication Lag chart](#metrics), the alert Severity and Condition must be Severe and Greater Than respectively.

To create an alert:

1. Navigate to **Admin > Alert Configurations > Alert Policies**.
1. Click **Create Alert Policy** and choose **Universe Alert**.
1. Set **Policy Template** to **Replication Lag**.
1. Enter a name and description for the alert policy.
1. Set **Target** to **Selected Universes** and select the source.
1. Set the conditions for the alert.

    - Enter a duration threshold. The alert is triggered when the lag exceeds the threshold for the specified duration.
    - Set the **Severity** option to Severe.
    - Set the **Condition** option to Greater Than.
    - Set the threshold to an acceptable value for your deployment. The default threshold is 3 minutes (180000ms).

1. Click **Save** when you are done.

When replication is set up, YugabyteDB automatically creates an alert for _YSQL Tables in DR/xCluster Config Inconsistent With Primary/Source_. This alert fires when tables are added or dropped from source's databases under replication, but are not yet added or dropped from the YugabyteDB Anywhere replication configuration.

For more information on alerting in YugabyteDB Anywhere, refer to [Alerts](../../../alerts-monitoring/alert/).

## Add a database to an existing replication

Note that, although you don't need to create objects on the target during initial setup, when you add a new database to an existing replication configuration, you _do_ need to create the same objects on the target. If source and target objects don't match, you won't be able to add the database to replication.

To add a database to replication, do the following:

1. Navigate to the source universe **xCluster Replication** tab and select the replication configuration.

1. Click **Actions > Select Databases and Tables**.

1. Select the databases to be copied to the target for replication.

    You can add databases containing colocated tables to the replication configuration as long as the underlying database is v2.18.1.0 or later. Colocated tables on the source and target should be created with the same colocation ID if they already exist on both the source and target prior to setup. Refer to [xCluster and colocation](../../../../architecture/docdb-sharding/colocated-tables/#xcluster-and-colocation).

1. Click **Validate Selection**.

    YugabyteDB Anywhere checks whether or not data needs to be copied to the target for the selected databases and its tables.

1. If data needs to be copied, click **Next: Confirm Full Copy**.

1. Click **Apply Changes**.

YugabyteDB Anywhere proceeds to copy the database to the target. How long this takes depends mainly on the amount of data that needs to be copied.

## Restart replication

Some situations, such as extended network partitions between the source and target, can cause a permanent failure of replication due to WAL logs being no longer available on the source.

In these cases, restart replication as follows:

1. Navigate to the universe **xCluster Replication** tab and select the replication configuration.

1. Click **Actions** and choose **Restart Replication**.
1. Select the databases to be copied to the target.
1. Click **Next: Configure Full Copy** and choose the storage configuration.
1. Click **Create a New Full Copy**.

This performs a full copy of the databases (YSQL) or tables (YCQL) involved from the source to the target.

## Pause and remove replication

To pause xCluster replication for a universe, do the following:

1. Navigate to the universe **xCluster Replication** tab and select the replication configuration.

1. Click **Pause Replication**.

To remove xCluster replication for a universe, do the following:

1. Navigate to the universe **xCluster Replication** tab and select the replication configuration.

1. Click **Actions** and choose **Delete Replication**.

You can opt to ignore errors and force delete the replication, but this is not recommended except in some rare scenarios. You can use this option to delete the xCluster configuration when one of the universes is inaccessible. However, doing so may cause some state information related to replication to be left behind on the underlying YugabyteDB universes that will require manual cleanup later on.

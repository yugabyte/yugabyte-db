---
title: Point-in-time recovery in YugabyteDB Aeon
headerTitle: Point-in-time recovery
linkTitle: Point-in-time recovery
description: Using Point-in-time recovery in YugabyteDB Aeon
headContent: Restore to a point in time
menu:
  preview_yugabyte-cloud:
    identifier: aeon-pitr
    parent: cloud-clusters
    weight: 210
type: docs
---

To prevent data loss, YugabyteDB Aeon supports point-in-time recovery (PITR) of cluster data. When enabled for a database or keyspace, YugabyteDB takes a snapshot of the data once a day. Each snapshot maintains a continuous change history. You can then recover to a specific point in time in a snapshot.

PITR is particularly applicable to the following:

- DDL errors, such as an accidental table removal.
- DML errors, such as execution of an incorrect update statement against one of the tables.

You can change the retention period for snapshots. The default is seven days, which gives you a rolling history of seven snapshots (one a day), with the oldest snapshot being deleted automatically as the most recent one is added.

For more information on PITR in YugabyteDB, refer to [Point-in-time recovery](../../../manage/backup-restore/point-in-time-recovery/).

To configure point in time recovery, and restore to a point in time, go to the cluster **Backups** tab and choose **Point in time Recovery**.

## Create a PITR configuration

You can create a PITR configuration as follows:

1. Navigate to the cluster **Backups** tab and choose **Point in time Recovery** to view a list of the databases and keyspaces already enabled for PITR, if any.

   ![PITR](/images/yp/pitr-main.png)

   If there are currently no databases or keyspaces enabled for PITR, a message is displayed.

1. Click **Enable Point-in-time Recovery**.

1. Select YSQL or YCQL, then select the databases or keyspaces for which to enable PITR.

1. Click **Next**.

1. Set the retention window for PITR.

1. Click **Enable Point in time Recovery**.

The database or keyspace is added to the **Databases/Keyspaces with Point-In-Time Recovery Enabled** list.

## Clone to a point in time

You can clone a database or keyspace at a specific point in time as follows:

1. Navigate to **Point-in-time Recovery**.

1. Find the database or keyspace you want to recover and click **Clone to Point in Time**.

1. In the **Recover dbname to a point in time** dialog shown in the following illustration, specify the recovery time parameters that fall within your predefined retention period:

    ![Recover](/images/yp/pitr-recover.png)

1. Click **Recover**.

## Disable a PITR configuration

You can disable PITR for a database or keyspace as follows:

1. Navigate to **Point-in-time Recovery**.

1. Find the database or keyspace for which you to disable PITR, click the three dots (**...**) to display its actions, and then select **Disable Point-in-Time Recovery**.

## Caveats and limitations

Enabling PITR impacts both disk consumption and performance. Keep in mind the following:

- When you increase the number of stored snapshots (by increasing the retention period of the snapshots), you also increase the amount of space required for the database. The amount of storage required also depends on the workload. When enabled, monitor your storage consumption alerts and add disk space or reduce the retention period if necessary.
- If you notice an impact on performance, refer to [Operational considerations](../../../manage/backup-restore/point-in-time-recovery/#operational-considerations) for guidance about further tuning.

    In addition to the snapshot retention period, YugabyteDB allows you to adjust the snapshot interval, which in YugabyteDB Aeon is fixed at 24 hours.

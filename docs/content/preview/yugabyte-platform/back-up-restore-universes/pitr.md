---
title: Point-in-time recovery in YugabyteDB Anywhere
headerTitle: Point-in-time recovery
linkTitle: Point-in-time recovery
description: Using Point-in-time recovery in YugabyteDB Anywhere
headContent: Restore to a point in time
menu:
  preview_yugabyte-platform:
    parent: back-up-restore-universes
    identifier: pitr
    weight: 50
type: docs
---

To prevent data loss, YugabyteDB Anywhere supports point-in-time recovery (PITR) of the universe data. When enabled for a database or keyspace, YugabyteDB Anywhere takes a snapshot of the data once a day. Each snapshot maintains a continuous change history. You can then recover to a specific point in time in a snapshot.

PITR is particularly applicable to the following:

- DDL errors, such as an accidental table removal.
- DML errors, such as execution of an incorrect update statement against one of the tables.

You can change the retention period for snapshots. The default is seven days, which gives you a rolling history of seven snapshots (one a day), with the oldest snapshot being deleted automatically as the most recent one is added.

For more information on PITR in YugabyteDB, refer to [Point-in-time recovery](../../../manage/backup-restore/point-in-time-recovery/).

## Caveats and limitations

Enabling PITR impacts both disk consumption and performance. Keep in mind the following:

- When you increase the number of stored snapshots (by increasing the retention period of the snapshots), you also increase the amount of space required for the database. The amount of storage required also depends on the workload. When enabled, monitor your storage consumption alerts and add disk space or reduce the retention period if necessary.
- If you notice an impact on performance, refer to [Operational considerations](../../../manage/backup-restore/point-in-time-recovery/#operational-considerations) for guidance about further tuning.

   In addition to the snapshot retention period, YugabyteDB allows you to adjust the snapshot interval, which in YugabyteDB Anywhere is fixed at 24 hours. Note, however, that if you use the yb-admin CLI to make changes to the PITR configuration of a universe managed by YugabyteDB Anywhere, including creating schedules and snapshots, your changes *are not* reflected in YugabyteDB Anywhere.

## Create a PITR configuration

You can create a PITR configuration as follows:

1. Navigate to **Universes**.

1. Select the name of the universe for which you want to use PITR.

1. Select the **Backups** tab, and then select **Point-in-time Recovery** to view a list of the databases and keyspaces already enabled for PITR, if any, as per the following illustration:

   ![PITR](/images/yp/pitr-main.png)

   If there are currently no databases or keyspaces enabled for PITR, a message is displayed.

1. Click **Enable Point-in-time Recovery** to open the dialog shown in the following illustration:

   ![Enable PITR](/images/yp/enable-pitr.png)

1. Complete the **Enable Point-in-time Recovery** dialog by selecting YSQL or YCQL as the API type, then selecting the database or keyspace for which to enable PITR, and then selecting the data snapshot retention period.

1. Click **Enable**.

The database or keyspace is now added to the **Databases/Keyspaces with Point-In-Time Recovery Enabled** list.

## Recover to a point in time

You can recover a snapshot to a specific point in time as follows:

1. Navigate to **Point-in-time Recovery**.

2. Find the database or keyspace whose snapshot you want to recover, click the three dots (**...**) to display its actions, and select **Recover to a Point in Time**.

3. In the **Recover dbname to a point in time** dialog shown in the following illustration, specify the recovery time parameters that fall within your predefined retention period:

   ![Recover](/images/yp/pitr-recover.png)

4. Click **Recover**.

## Disable a PITR configuration

You can disable PITR as follows:

1. Navigate to **Point-in-time Recovery**.

2. Find the database or keyspace for which you to disable PITR, click the three dots (**...**) to display its actions, and then select **Disable Point-in-Time Recovery**.

3. Use the dialog shown in the following illustration to confirm that your intention is to disable PITR for the database or keyspace:

   ![Disable](/images/yp/pitr-disable.png)

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

To prevent data loss, YugabyteDB Anywhere supports [point-in-time recovery](../../../manage/backup-restore/point-in-time-recovery/) (PITR) of the universe data.

{{< note title="Note" >}}

You must initiate and manage PITR using the YugabyteDB Anywhere UI. If you use the yb-admin CLI to make changes to the PITR configuration of a universe managed by YugabyteDB Anywhere, including creating schedules and snapshots, your changes are not reflected in YugabyteDB Anywhere.

{{< /note >}}

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

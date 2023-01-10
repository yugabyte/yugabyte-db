---
headerTitle: Point-in-time recovery
linkTitle: Point-in-time recovery
description: Point-in-time recovery
menu:
  preview_yugabyte-platform:
    parent: back-up-restore-universes
    identifier: pier
    weight: 10
type: docs
---

To prevent data loss, YugabyteDB Anywhere supports [point-in-time recovery](../../../manage/backup-restore/point-in-time-recovery/) (PITR) of the universe data.

<!--

Before using YugabyteDB Anywhere to perform PITR, you need to enable PITR for your database by creating a [snapshot schedule](../../../manage/backup-restore/point-in-time-recovery/#create-a-schedule).

-->

## Create a PITR configuration

You can create a PITR configuration as follows:

1. Navigate to **Universes**.

1. Select the name of the universe for which you want to use PITR.

1. Select the **Backups** tab and then select **Point-in-time Recovery**.

1. View the list of the databases and keyspaces already enabled for PITR, if any, as per the following illustration:

   ![PITR](/images/yp/pitr-main.png)

1. Click **Enable Point-in-time Recovery** to open the dialog shown in the following illustration:

   ![Enable PITR](/images/yp/enable-pitr.png)

1. Complete the **Enable Point-in-time Recovery** dialog by selecting YSQL or YCQL as the API type, then selecting the database or keyspace to enable for PITR, and then selecting the data snapshot retention period.

1. Click **Enable**.

The database or keyspace is now added to the **Databases/Keyspaces with Point-In-Time Recovery Enabled** list. 

## Recover to a point-in-time 







## Disable a PITR configuration
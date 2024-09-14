---
title: Schedule universe backups
headerTitle: Schedule universe backups
linkTitle: Schedule data backups
description: Use YugabyteDB Anywhere to create scheduled backups of universe data.
headContent: Create backup schedules to regularly back up universe data
menu:
  stable_yugabyte-platform:
    identifier: schedule-data-backups
    parent: back-up-restore-universes
    weight: 15
type: docs
---

You can use YugabyteDB Anywhere to perform regularly scheduled backups of YugabyteDB universe data for all tables in a database (YSQL) or keyspace (YCQL) or only the specified tables (YCQL only).

To back up your universe data immediately, see [Back up universe data](../back-up-universe-data/).

To schedule backups, backups must be enabled for the universe. On the universe **Tables** tab, click **Actions** to verify that backups are enabled. If disabled, click **Enable Backup**.

## Create a scheduled backup policy

Before scheduling a backup of your universe data, create a policy, as follows:

1. Select the **Backups** tab and then select **Scheduled Backup Policies**.

1. Click **Create Scheduled Backup Policy** to open the dialog shown in the following illustration:

    ![Create Scheduled Backup](/images/yp/scheduled-backup-ysql.png)

1. Provide the backup policy name.

1. Select the API type for the backup.

1. Select the storage configuration. For more information, see [Configure backup storage](../configure-backup-storage/).

1. Select the database/keyspace to back up.

1. For YCQL backups, you can choose to back up all tables in the keyspace to which the database belongs or only certain tables. If you choose **Select a subset of tables**, a **Select Tables** dialog opens allowing you to select one or more tables to back up. When finished, click **Confirm**.

1. For YSQL backups of universes with geo-partitioning, you can choose to back up the tablespaces. Select the **Backup tablespaces information** option.

    If you don't choose to back up tablespaces, the tablespaces are not preserved and their data is backed up to the primary region.

1. Specify the period of time during which the backup is to be retained. Note that there's an option to never delete the backup.

1. Specify the interval between backups or select **Use cron expression (UTC)**.

1. Enable **Take incremental backups within full backup intervals** to instruct the schedule policy to take full backups periodically and incremental backups between those full backups (YBA version 2.16 or later, and YugabyteDB version 2.16 or later only). The incremental backups intervals must be shorter than the full scheduled backup frequency:

    ![Incremental Backup](/images/yp/scheduled-backup-ycql-incremental.png)

    If you disable the full backup, the incremental backup stops. If you enable the full backup again, the incremental backup schedule starts on new full backups.

    If you delete the main full backup schedule, the incremental backup schedule is also deleted.

    You cannot modify any incremental backup-related property in the schedule; to overwrite any incremental backup property, you have to delete the existing schedule and create a new schedule if needed.

1. Click **Create**.

Subsequent backups are created based on the value you specified for **Set backup intervals** or **Use cron expression**.

## Disable backups

You can disable all backups, including scheduled ones, as follows:

1. Navigate to the universe's **Tables** tab.
1. Click **Actions > Disable Backup**.

## Delete a scheduled backup

You can permanently remove a scheduled backup, as follows:

1. Navigate to the universe's **Backups** tab.
1. Find the scheduled backup in the **Backups** list and click **... > Delete Backup**.

To delete a policy, select **Scheduled Backup Policies**, find the policy and click its **Actions > Delete Policy**.

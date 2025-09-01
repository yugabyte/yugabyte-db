---
title: Schedule universe backups
headerTitle: Schedule universe backups
linkTitle: Schedule data backups
description: Use YugabyteDB Anywhere to create scheduled backups of universe data.
headContent: Create backup schedules to regularly back up universe data
menu:
  v2024.2_yugabyte-platform:
    identifier: schedule-data-backups
    parent: back-up-restore-universes
    weight: 15
type: docs
---

You can use YugabyteDB Anywhere to perform regularly scheduled backups of YugabyteDB universe data for all tables in a database (YSQL) or keyspace (YCQL) or only the specified tables (YCQL only).

To back up your universe data immediately, see [Back up universe data](../back-up-universe-data/).

To schedule backups, backups must be enabled for the universe. On the universe **Tables** tab, click **Actions** to verify that backups are enabled. If disabled, click **Enable Backup**.

## Create a scheduled backup policy

{{<tags/feature/ea idea="989">}}You can create scheduled backups with or without PITR. To enable the feature in YugabyteDB Anywhere, set the **Option for Off-Cluster PITR based Backup Schedule** Global Runtime Configuration option (config key `yb.ui.feature_flags.off_cluster_pitr_enabled`) to true. Refer to [Manage runtime configuration settings](../../administer-yugabyte-platform/manage-runtime-config/). Note that only a Super Admin user can modify Global configuration settings.

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

1. Enable **Take incremental backups within full backup intervals** to instruct the schedule policy to take full backups periodically and incremental backups between those full backups (YugabyteDB Anywhere version 2.16 or later, and YugabyteDB version 2.16 or later only). The incremental backups intervals must be shorter than the full scheduled backup frequency:

    ![Incremental Backup](/images/yp/scheduled-backup-ycql-incremental.png)

    If you disable the full backup, the incremental backup stops. If you enable the full backup again, the incremental backup schedule starts on new full backups.

    If you delete the main full backup schedule, the incremental backup schedule is also deleted.

    You cannot modify any incremental backup-related property in the schedule; to overwrite any incremental backup property, you have to delete the existing schedule and create a new schedule if needed.

1. Click **Create**.

Subsequent backups are created based on the value you specified for **Set backup intervals** or **Use cron expression**.

## Create a scheduled backup policy with PITR

<!--
Creating a scheduled backup policy with PITR is currently {{<tags/feature/ea>}}.

To create a scheduled backup policy with PITR support, you can set the `enablePointInTimeRestore` attribute to true in the API request. For example:

```shell
curl 'http://<platform-url>/api/v1/customers/:cUUID/create_backup_schedule_async' \
  -d '{
    "backupType": "YQL_TABLE_TYPE",
    "customerUUID": "f33e3c9b-75ab-4c30-80ad-cba85646ea39",
    "sse": true,
    "storageConfigUUID": "20946d96-978f-4577-ae28-c156eebb6aad",
    "universeUUID": "816ecdcd-8031-4a41-ad62-f49d8a2aa6dc",
    "tableByTableBackup": false,
    "useTablespaces": true,
    "keyspaceTableList": [],
    "timeBeforeDelete": 86400000,
    "expiryTimeUnit": "DAYS",
    "scheduleName": "PIT-test-ycql-2",
    "schedulingFrequency": 86400000,
    "frequencyTimeUnit": "DAYS",
    "incrementalBackupFrequencyTimeUnit": "MINUTES",
    "incrementalBackupFrequency": 900000,
    "enablePointInTimeRestore": true
  }'
```

Steps to create the sceduled backup policy via the UI when the runtime config flag is available in 2024.2.1.0 -->

{{<tags/feature/ea idea="989">}}You can create scheduled backups with or without PITR. To enable the feature in YugabyteDB Anywhere, set the **Option for Off-Cluster PITR based Backup Schedule** Global Runtime Configuration option (config key `yb.ui.feature_flags.off_cluster_pitr_enabled`) to true. Refer to [Manage runtime configuration settings](../../administer-yugabyte-platform/manage-runtime-config/). Note that only a Super Admin user can modify Global configuration settings.

Before scheduling a backup of your universe data, create a policy, as follows:

1. Navigate to your universe, select **Backups > Scheduled Backup Policies**, and click **Create Scheduled Backup Policy** to open the **Create Scheduled Backup Policy** wizard.

    ![Create Scheduled Backup](/images/yp/create-schedule-backup-pitr.png)

1. Provide a name for the backup policy, and select the storage configuration. For more information, see [Configure backup storage](../configure-backup-storage/). When finished, click **Next**.

1. Select the Keyspaces/Databases you want to back up, either YSQL or YCQL.

    For YSQL, you have a additional **Advanced Configuration** option to include **Backup tablespace information** (enabled by default). If you don't choose to back up tablespaces, the tablespaces are not preserved and their data is backed up to the primary region.

    For YCQL, you can choose to back up all tables or a selection of tables. Click **Select a subset of tables** to display the **Select Tables** dialog, where you can select one or more tables to back up, and click **Confirm**.

    When finished, click **Next**.

1. Select a backup strategy:

    - **Standard backup** (without PITR support), or
    - **Backup with ability to restore to point-in-time**

    Note that, with point-in-time enabled backups, you cannot restore to a point in time earlier than the most recent DDL change. To restore to a time prior to the DDL change, you need a backup that was performed prior to the change.

    Specify the interval between backups or select **Use cron expression (UTC)**.

    Enable **Take incremental backups within full backup intervals** to instruct the schedule policy to take full backups periodically and incremental backups between those full backups (supported in YugabyteDB Anywhere v2.16 or later, and YugabyteDB v2.16 or later only). The incremental backup intervals must be shorter than the full scheduled backup frequency.

    Specify the time period to retain a backup, or select **Keep indefinitely** to never delete the backup.

    When finished, click **Next**.

1. Review the backup policy summary to ensure all details are correct, and click **Create Scheduled Backup Policy** to finalize and create the policy.

You should see an _in progress_ notification indicating the backup creation, and as the process is asynchronous, it may take a few minutes to complete.

After the backup creation process is complete, the policy will be automatically enabled as per the following illustration:

![Scheduled Backup policy](/images/yp/schedule-backup-policy-pitr.png)

Backups created with PITR support will show "Enabled" status in the Point-in-Time Restore column of the **Backups** list. Hover over this status to view the restore window's start and end times as per the following illustration:

![Restore window](/images/yp/restore-window-pitr.png)

### Edit a scheduled backup policy

You can change the backup frequency of a scheduled backup policy as follows:

1. Navigate to your universe and select **Backups > Scheduled Backup Policies**.
1. For your scheduled backup, click **Actions > Edit Policy**.
1. Change the interval between backups or select **Use cron expression (UTC)**.
1. Click **Save**.

## Disable backups

You can disable backups, including scheduled ones, as follows:

1. Navigate to your universe and select **Backups > Scheduled Backup Policies**.
1. Disable the policy managing the backup you want to disable.

## Delete a scheduled backup and policy

You can permanently remove a scheduled backup, as follows:

1. Navigate to your universe and select **Backups > Backups**.
1. Find the scheduled backup in the **Backups** list and click **... > Delete Backup**.

To delete a policy, select **Scheduled Backup Policies**, find the policy and click its **Actions > Delete Policy**.

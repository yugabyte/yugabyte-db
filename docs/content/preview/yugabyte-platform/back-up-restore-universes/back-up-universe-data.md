---
title: Back up universe data
headerTitle: Back up universe data
linkTitle: Back up universe data
description: Use YugabyteDB Anywhere to back up data.
headContent: Create full and incremental backups
aliases:
  - /preview/back-up-restore-universes/back-up-universe-data/ycql/
  - /preview/back-up-restore-universes/back-up-universe-data/ysql/
menu:
  preview_yugabyte-platform:
    parent: back-up-restore-universes
    identifier: back-up-universe-data
    weight: 20
type: docs
---

You can use YugabyteDB Anywhere to back up your YugabyteDB universe data. This includes actions such as deleting and restoring the backup, as well as restoring and copying the database location.

If you are using YBA version 2.16 or later to manage universes with YugabyteDB version 2.16 or later, you can additionally create [incremental backups](#create-incremental-backups) and [configure backup performance parameters](#configure-backup-performance-parameters).

For information on how to schedule backups for a later time or as a recurring task, see [Schedule universe backups](../schedule-data-backups/).

Note that non-transactional backups are not supported.

To view, [restore](../restore-universe-data/), or delete existing backups for your universe, navigate to that universe and select **Backups**.

By default, the list displays all the backups generated for the universe regardless of the time period. You can configure the list to only display the backups created during a specific time period, such as last year, last month, and so on. In addition, you can specify a custom time period.

## Create backups

The universe **Backups** page allows you to create new backups that start immediately, as follows:

1. Navigate to the universe and select **Backups**, then click **Backup now** to open the dialog shown in the following illustration:

    ![Backup](/images/yp/create-backup-new-3.png)

1. Select the API type for the backup.

1. Select the storage configuration you want to use for the backup. For more information, see [Configure backup storage](../configure-backup-storage/).

1. Select the database (YSQL) or keyspace (YCQL) to back up.

1. For YCQL backups, you can choose to back up all tables in the keyspace to which the database belongs or only certain tables. Click **Select a subset of tables** to display the **Select Tables** dialog, where you can select one or more tables to back up. Click **Confirm** when you are done.

1. Specify the period of time during which the backup is to be retained. Note that there's an option to never delete the backup.

1. If you are using YBA version prior to 2.16 to manage universes with YugabyteDB version prior to 2.16, you can optionally specify the number of threads that should be available for the backup process.

1. Click **Backup**.

If the universe has [encryption at rest enabled](../../security/enable-encryption-at-rest), data files are backed up as-is (encrypted) to reduce the computation cost of a backup and to keep the files encrypted. A universe key metadata file containing key references is also backed up.

For YSQL, you can allow YugabyteDB Anywhere to back up your data with the user authentication enabled by following the instructions in [Edit configuration flags](../../manage-deployments/edit-config-flags) to add the `ysql_enable_auth=true` and `ysql_hba_conf_csv="local all all trust"` YB-TServer flags.

### View backup details

To view detailed information about an existing backup, click on it to open **Backup Details**.

To access a list of all backups from all universes, including deleted universes, navigate to **Backups** on the YugabyteDB Anywhere left-side menu.

## Create incremental backups

You can use **Backup Details** to add an incremental backup (YBA version 2.16 or later and YugabyteDB version 2.16 or later only).

Incremental backups are taken on top of a complete backup. To reduce the length of time spent on each backup, only SST files that are new to YugabyteDB and not present in the previous backups are incrementally backed up. For example, in most cases, for incremental backups occurring every hour, the 1-hour delta would be significantly smaller compared to the complete backup. The restore happens until the point of the defined increment.

You can create an incremental backup on any complete or incremental backup taken using YB-Controller, as follows:

1. Navigate to **Backups**, select a backup, and then click on it to open **Backup Details**.

1. In the  **Backup Details** view, click **Add Incremental Backup**.

1. On the **Add Incremental Backup** dialog, click **Confirm**.

A successful incremental backup appears in the list of backups.

You can delete only the full backup chain which includes a complete backup and its incremental backups. You cannot delete a subset of successful incremental backups.

A failed incremental backup, which you can delete, is reported similarly to any other failed backup operations.

## Configure backup performance parameters

If you are using YBA version 2.16 or later to manage universes with YugabyteDB version 2.16 or later, you can manage the speed of backup and restore operations by configuring resource throttling.

To configure throttle parameters:

1. Navigate to the universe and select the **Backups** tab.

1. Click **Advanced** and choose **Configure Throttle Parameters** to display the **Configure Resource Throttling** dialog.

    ![Throttle](/images/yp/backup-restore-throttle.png)

1. For faster backups and restores, enter higher values. For lower impact on database performance, enter lower values.

1. Click **Save**.

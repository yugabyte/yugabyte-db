---
title: Back up universe data
headerTitle: Back up universe data
linkTitle: Back up universe data
description: Use YugabyteDB Anywhere to back up data.
headContent: Create full and incremental backups
menu:
  stable_yugabyte-platform:
    parent: back-up-restore-universes
    identifier: back-up-universe-data
    weight: 20
type: docs
---

You can use YugabyteDB Anywhere to back up your YugabyteDB universe data. This includes actions such as deleting and restoring the backup, as well as restoring and copying the database location.

Before you can back up universes, you need to [configure a storage location](../configure-backup-storage/) for your backups.

If you are using YBA version 2.16 or later to manage universes with YugabyteDB version 2.16 or later, you can additionally create [incremental backups](#create-incremental-backups) and [configure backup performance parameters](#configure-backup-performance-parameters).

For information on how to schedule backups for a later time or as a recurring task, see [Schedule universe backups](../schedule-data-backups/).

Note that non-transactional backups are not supported.

To view, [restore](../restore-universe-data/), or delete existing backups for your universe, navigate to that universe and select **Backups**.

By default, the list displays all the backups generated for the universe regardless of the time period. You can filter the list to only display the backups created during a specific time period, such as last year, last month, and so on. In addition, you can specify a custom time period.

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

To view detailed information about an existing backup, click on the backup (row) in the **Backups** list to open **Backup Details**.

The **Backup Details** include the storage address of your backup. In the list of databases (YSQL) or keyspaces (YCQL), click **Copy Location** for the database or keyspace. If your backup includes incremental backups, click the arrow for the increment of interest to display the databases or keyspaces.

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

## Identifying backup components

When restoring a YugabyteDB database from your backup storage (S3, Azure, or GCP bucket), it is important to identify the set of folders that make up a complete backup. The following sections describe how to identify backup components to achieve a good backup.

### Path components of a backup

Following is sample path component set and its description in a table to aid in identifying a complete backup.

```output
s3://user_bucket/some/sub/folders
   /univ-a85b5b01-6e0b-4a24-b088-478dafff94e4
     /ybc_backup-92317948b8e444ba150616bf182a061
       /incremental
         /20204-01-04T12: 11: 03
           /multi-table-postgres_40522fc46c69404893392b7d92039b9e
```

| Components | Description |
| :--------- | :---------- |
| Backup location | Location configured in YugabyteDB Anywhere (YBA). Alternate sub-folders within the S3 bucket may be included in the backup location definitions, and used to disambiguate multiple clusters managed by one YBA (For example, s3://user_bucket/east1, and s3://user_bucket/east2 as 2 locations sharing different folders in the same bucket (user_bucket), but used by east1 or east2 clusters, respectively). |
| Universe UUID | A YBA component which guarantees that universes with identical human-created names sharing a bucket are not subject to collision. |
| Backup Series Name and UUID | A human-readable backup series name and YBA generated UUID. The UUID ensures that YBA can correctly identify the appropriate folder if the human-readable portions collide. |
| Full (or Incremental) | Indicates if further sub-folder(s) contain Full or Incremental backups. |
| Creation time | The time when Full or Incremental backup started. |
| The backup | A folder that includes files (metadata and success) and subfolders (tablet components). |

### Folders required for restore

The path components must remain unchanged for restore to work. The backup location _may_ be a different bucket name, but rest of the path components must match, for example, `s3://user_bucket/some/sub/folders/…` changed to `s3://new_user_bucket/some/sub/folders/…` retaining the sub-folders.

A "Backup set" consists of a successful Full Backup, and one or more consecutive successful Incremental Backup(s), and may be used to restore a database at the point in time of Full Backup and/or the time(s) of "good" Incremental Backups, as long as the chain of "good" Incrementals is unbroken. The creation time component is beneficial to identify Incremental(s) occurring after a Full Backup.

When YBA writes a backup to a bucket, the last step after all parallel tasks complete is writing a "success" file to the backup folder which qualifies as a good backup. A Full or Incremental backup without a success file should not be assumed good, and an older backup should be used for restore instead.

![Success file metadata](/images/yp/success-file-backup.png)

### Finding backup folders

In some cases, the bucket name may be difficult to determine from the backup location (For example, `s3://backups.my_domain.com/folder`). For this and other cases where the location is not obvious, navigate to your universe and select **Backups** to expand it, and then click **Copy Location**.
---
title: Back up universe data
headerTitle: Back up universe data
linkTitle: Back up universe data
description: Use YugabyteDB Anywhere to back up data.
headContent: Create full and incremental backups
aliases:
  - /stable/yugabyte-platform/back-up-restore-universes/back-up-universe-data/ycql/
  - /stable/yugabyte-platform/back-up-restore-universes/back-up-universe-data/ysql/
menu:
  stable_yugabyte-platform:
    parent: back-up-restore-universes
    identifier: back-up-universe-data
    weight: 20
type: docs
---

You can use YugabyteDB Anywhere to back up your universe data. This includes deleting and restoring backups, as well as restoring and copying the database location.

Before you can back up universes, you need to [configure a storage location](../configure-backup-storage/) for your backups.

If you are using v2.16 or later to manage universes with YugabyteDB v2.16 or later, you can additionally create [incremental backups](#create-incremental-backups) and [configure backup performance parameters](#configure-backup-performance-parameters).

For information on how to schedule backups for a later time or as a recurring task, see [Schedule universe backups](../schedule-data-backups/).

Note that non-transactional backups are not supported.

To view, [restore](../restore-universe-data/), or delete existing backups of your universe, navigate to the universe and select **Backups**.

By default, the list displays all the backups generated for the universe regardless of the time period. You can filter the list to only display the backups created during a specific time period, such as last year, last month, and so on. In addition, you can specify a custom time period.

## Create backups

The universe **Backups** page allows you to create new backups that start immediately, as follows:

1. Navigate to the universe and select **Backups**, then click **Backup now**.

    ![Backup](/images/yp/create-backup-ysql-20252.png)

1. Select the API type for the backup.

1. Select the storage configuration you want to use for the backup. For more information, see [Configure backup storage](../configure-backup-storage/).

1. Select the database (YSQL) or keyspace (YCQL) to back up.

1. For YCQL backups, you can choose to back up all tables in the keyspace to which the database belongs or only certain tables. Click **Select a subset of tables** to display the **Select Tables** dialog, where you can select one or more tables to back up. Click **Confirm** when you are done.

1. For YSQL backups of universes with geo-partitioning, you can choose to back up the tablespaces. Select the **Backup tablespaces information** option.

    If you don't choose to back up tablespaces, the tablespaces are not preserved and their data is backed up to the primary region.

1. Specify the period of time during which the backup is to be retained. Note that there's an option to never delete the backup.

1. To back up database roles (YSQL only), choose the **Backup global roles** option.

1. Click **Backup**.

If the universe has [encryption at rest enabled](../../security/enable-encryption-at-rest), data files are backed up as-is (encrypted) to reduce the computation cost of a backup and to keep the files encrypted. A universe key metadata file containing key references is also backed up.

### View backup details

To view detailed backup information, click on the backup (row) in the **Backups** list to open **Backup Details**.

The **Backup Details** include the storage address of your backup. If you want to [manually verify or access a backup](#access-backups-in-storage), you can use this address to access the backup where it is stored. To access the address, in the list of databases (YSQL) or keyspaces (YCQL), click **Copy Location** for the database or keyspace. If your backup includes incremental backups, click the arrow for the increment of interest to display the databases or keyspaces.

To access a list of all backups from all universes, including deleted universes, navigate to **Backups** on the YugabyteDB Anywhere left-side menu.

## Create incremental backups

You can use **Backup Details** to add an incremental backup (v2.16 or later and universe running YugabyteDB v2.16 or later only).

Incremental backups are taken on top of a complete backup. To reduce the length of time spent on each backup, only SST files that are new to YugabyteDB and not present in the previous backups are incrementally backed up. For example, in most cases, for incremental backups occurring every hour, the 1-hour delta would be significantly smaller compared to the complete backup. The restore happens until the point of the defined increment.

You can create an incremental backup on any complete or incremental backup taken using YB Controller, as follows:

1. Navigate to **Backups**, select a backup, and then click on it to open **Backup Details**.

1. In the  **Backup Details** view, click **Add Incremental Backup**.

1. On the **Add Incremental Backup** dialog, click **Confirm**.

A successful incremental backup appears in the list of backups.

You can delete only the full backup chain, which includes a complete backup and its incremental backups. You cannot delete a subset of successful incremental backups.

A failed incremental backup, which you can delete, is reported similarly to any other failed backup operation.

## Configure backup performance parameters

You can manage the speed of backup and restore operations and their impact on database performance by configuring the following parameters:

- Parallel uploads per node. Number of parallel uploads/downloads of tablets per node. For faster operation, enter higher values; for lower impact on database performance, enter lower values.
- Buffers per upload per node. Number of buffers used to read from disk and upload/download for a single tablet. For faster operation, enter higher values; for lower impact on database performance, enter lower values.
- Disk read/write bytes per second (in MB/s). You can rate-limit disk throughput during backup upload and restore download to reduce impact on cluster operations. The minimum value is 1 MB/s. To allow backup and restore to use as much throughput as they can (that is, no disk-based throttling), enter a value of 0 (the default).

Choose values that balance backup and restore speed with impact on production. Too high a value can consume disk IO that the database needs; too low a value can make backups and restores run longer. This is especially important when using incremental backups.

To configure throttle parameters:

1. Navigate to the universe and select the **Backups** tab.

1. Click **Advanced** and choose **Configure Throttle Parameters** to display the **Configure Resource Throttling** dialog.

    ![Throttle](/images/yp/backup-restore-throttle-2025-2.png)

1. Set resource parameters for backups and restores.

1. Click **Save**.

## Access backups in storage

You can manually access and review backups by navigating to the backup storage location. To obtain the location of a backup, display the **Backup Details** and click **Copy Location** for the database or keyspace. If your backup includes incremental backups, click the arrow for the increment of interest to display the databases or keyspaces.

The copied location provides the full path to the backup.

YugabyteDB Anywhere universe backups are stored using the following folder structure:

```output
<storage-address>
  /sub-directories
    /<univ_name>_<universe-uuid>
     /<database-name>
      /<backup-series-name>-<backup-series-uuid>
        /<backup-type>
          /<creation-time>
            /<backup-name>_<uuid>
```

For example:

```output
s3://user_bucket
  /some/sub/folders
    /universe-name_a85b5b01-6e0b-4a24-b088-478dafff94e4
     /database1_name
      /ybc_backup-92317948b8e444ba150616bf182a061
        /incremental
          /20204-01-04T12: 11: 03
            /multi-table-postgres_40522fc46c69404893392b7d92039b9e
```

| Component | Description |
| :-------- | :---------- |
| Storage address | The name of the bucket as specified in the [storage configuration](../configure-backup-storage/) that was used for the backup. |
| Sub-directories | The path of the sub-folders (if any) in a bucket. |
| Universe name and UUID | The name of the universe and UUID that was backed up. You can move this folder to a different location, but to successfully restore, do not modify this folder, or any of its contents. |
| Database or Keyspace name | The name of the Database or Keyspace that was backed up. |
| Backup series name and UUID | The name of the backup series and YBA-generated UUID. The UUID ensures that YBA can correctly identify the appropriate folder. |
| Backup type | `full` or `incremental`. Indicates whether the subfolders contain full or incremental backups. |
| Creation time | The time the backup was started. |
| Backup name and UUID | The name of the backup and YBA-generated UUID that uniquely identifies the backup object. This folder contains the backup files (metadata and success) and subfolders (tablet components). |

You can also use [yba-cli](../../anywhere-automation/anywhere-cli/) to get a list of backups and their details as follows:

```sh
yba-cli backup list
```

```output
Backup UUID                            Universe                                     Storage Configuration                          Storage Configuration Type   KMS configuration   Backup Type        Completion Time                   Has Incremental Backups   State
b924cda0-7330-4870-8984-8e31b61007c4   test(2341a0b9-7b60-4a21-b6f6-6b557e5df036)   aws-s3(6a469b14-d319-4173-a8db-b9d972f875ab)   S3                                               PGSQL_TABLE_TYPE   Sun, 29 Jun 2025 20:27:11 +0000   false                     Completed
No more backups present
```

```sh
yba-cli backup  describe --uuid b924cda0-7330-4870-8984-8e31b61007c4
```

```output
General
Backup UUID                            Backup Type        Category        State
b924cda0-7330-4870-8984-8e31b61007c4   PGSQL_TABLE_TYPE   YB_CONTROLLER   Completed

Universe                                     Schedule Name   Has Incremental Backups
test(2341a0b9-7b60-4a21-b6f6-6b557e5df036)                   false

Storage Configuration                          Storage Configuration Type   KMS configuration
aws-s3(6a469b14-d319-4173-a8db-b9d972f875ab)   S3

Create Time                       Completion Time                   Expiry Time
Sun, 29 Jun 2025 20:27:05 +0000   Sun, 29 Jun 2025 20:27:11 +0000

Keyspace Details
Keyspace 1 Details
Keyspace   Backup size   Default Location
yugabyte   63.95 MB      s3://yb-emea-poc-backups/univ-2341a0b9-7b60-4a21-b6f6-6b557e5df036/ybc_backup-b924cda07330487089848e31b61007c4/full/2025-06-29T20:27:05/multi-table-yugabyte_6e7945f5b3e84a7a97eb3d8033b92f9c

Table UUID list
[]

Table name list
[]
```

A backup set consists of a successful full backup, and (if incremental backups were taken) one or more consecutive successful incremental backups. The backup set can be used to restore a database at the point in time of the full and/or incremental backup, as long as the chain of good incremental backups is unbroken. Use the creation time to identify increments that occurred after a full backup.

When YugabyteDB Anywhere writes a backup, the last step after all tasks complete is to write a "success" file to the backup folder. The presence of this file is verification of a good backup. Any full or incremental backup that does not include a success file should not be assumed to be good, and you should use an older backup for restore instead.

![Success file metadata](/images/yp/success-file-backup.png)

### Moving backups between buckets

When moving a backup (for example, for long term storage), be sure to include all the sub-directories below the storage address.

For a successful restore at a later date, none of the sub-components and folder names can be modified (from the sub-directories on down) in the address - only the storage address.

For example, if you have a backup as follows:

```output
s3://test_bucket/test/univ-xyz
```

You can move the backup to a location similar to the following:

```output
s3://user_bucket/test/univ-xyz
```

However, you can't move it to a different sub-directory inside the bucket such as the following:

```output
s3://user_bucket/new-test/univ-xyz
```

To restore from a backup that has been moved, you need to use the [Advanced restore procedure](../restore-universe-data/#advanced-restore-procedure).

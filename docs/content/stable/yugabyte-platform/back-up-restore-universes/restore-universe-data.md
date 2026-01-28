---
title: Restore universe data
headerTitle: Restore universe data
linkTitle: Restore universe data
description: Use YugabyteDB Anywhere to restore data.
headContent: Restore from full or incremental backups
aliases:
  - /stable/back-up-restore-universes/restore-universe-data/ycql/
  - /stable/back-up-restore-universes/restore-universe-data/ysql/
menu:
  stable_yugabyte-platform:
    parent: back-up-restore-universes
    identifier: restore-universe-data
    weight: 30
type: docs
---

To access backups from a specific universe, navigate to the universe and choose **Backups**.

To access all universe backups, navigate to **Backups**.

{{< warning title="Restoring a backup using YBC" >}}

Backups from a stable track universe can only be restored to a higher version stable track YugabyteDB universe, and the same applies for preview track. Optionally, you can set a runtime flag `yb.skip_version_checks`, to skip all YugabyteDB and YugabyteDB Anywhere version checks during restores. For more information, contact {{% support-platform %}}.

{{< /warning >}}

## Prerequisites

- The target universe must have enough nodes to accommodate the restore.
- If the source universe is encrypted, the KMS configuration that was used to encrypt the universe. See [Back up and restore data from an encrypted at rest universe](../../security/enable-encryption-at-rest/#back-up-and-restore-data-from-an-encrypted-at-rest-universe).
- If the source backup has tablespaces, to restore the tablespaces the target universe must have a matching topology; that is, the zones and regions in the target must be the same as the zones and regions in the source.

    If the topology of the target universe does not match, none of the tablespaces are preserved and all their data is written to the primary region. After the restore, you will have to re-add all the tablespaces. For more information on specifying data placement for tables and indexes, refer to [Tablespaces](../../../explore/going-beyond-sql/tablespaces/).

## Restore an entire or incremental backup

You can restore YugabyteDB universe data from a backup as follows:

1. In the **Backups** list, select the backup to restore to display the **Backup Details**.

    ![Restore YCQL](/images/yp/restore-ycql-backup-details.png)

1. Click **Restore Entire Backup**. Alternatively, if your backup includes incremental backups, you can restore a part of an incremental backup chain by selecting an increment from the list in the **Backup Details** view and clicking its **Restore to this point**.

1. In the **Restore Backup** dialog, select the universe (target) to which you want to restore the backup.

1. To rename databases (YSQL) or keyspaces (YCQL), select the **Rename** option.

    If you are restoring a backup to a universe with an existing databases of the same name, you must rename the database.

1. If you are restoring data from a universe that has tablespaces, select the **Restore tablespaces and data to their respective regions** option.

    To restore tablespaces, the target universe must have a topology that matches the source.

    You can restore without tablespaces, in which case none of the tablespaces are preserved and all their data is written to the primary region.

1. If the backup includes roles, choose the **Restore global roles** option to restore roles.

    Note that if the target universe already has matching roles, those roles are not overwritten.

1. If you are restoring data from a universe that has, or previously had, [encryption at rest enabled](../../security/enable-encryption-at-rest), then you must select the KMS configuration to use so that the master keys referenced in the metadata file can be retrieved.

    If the universe was previously encrypted at rest, but is not currently, then the retrieved keys assure that any existing files can be decrypted. The retrieved keys are used to build and augment the universe key registry on the restored universe with the required master keys. The universe data files are restored normally afterwards.

1. If you chose to rename databases or keyspaces, click **Next**, then enter new names for the databases or keyspaces that you want to rename.

1. Click **Restore**.

The restore begins immediately. When finished, a completed **Restore Backup** task appears under **Tasks > Task History**.

To confirm that the restore succeeded, select the **Tables** tab to compare the original table with the table to which you restored.

To view the details of a restored database, navigate to **Universes > your universe > Backups > Restore History**, or **Backups > Restore History**.

## Restore selected databases or keyspaces and tables

You can restore only a specific database (YSQL) or keyspace (YCQL).

In addition, if you are restoring a YCQL keyspace, you can choose the tables in the keyspace you want to restore.

For instructions on restoring a single table in YSQL, refer to [Restore a single table in YSQL](../restore-ysql-single-table).

To restore, do the following:

1. In the **Backups** list, select the backup to restore to display the **Backup Details**.

1. In the list of databases (YSQL) or keyspaces (YCQL), click **Restore** for the database or keyspace you want to restore. If your backup includes incremental backups, to display the databases or keyspaces, click the down arrow for the increment at which you want to restore.

1. In the **Restore Backup** dialog, select the universe (target) to which you want to restore the backup.

    Note that the Creation time shown in the dialog indicates the backup job start time, _not_ the time that the particular database was actually snapshotted and backed up. When multiple databases are included in a backup job, each databases is snapshotted and backed up in serial order; thus, each individual database's actual snapshot and backup time is not the same as the backup job start time. To restore a database to a specific point in time, use [Point-in-time recovery](../pitr/).

1. If you are restoring data from a universe that has, or previously had, [encryption at rest enabled](../../security/enable-encryption-at-rest), then you must select the KMS configuration to use so that the master keys referenced in the metadata file can be retrieved.

    If the universe was previously encrypted at rest, but is not currently, then the retrieved keys assure that any existing files can be decrypted. The retrieved keys are used to build and augment the universe key registry on the restored universe with the required master keys. The universe data files are restored normally afterwards.

1. To rename databases (YSQL) or keyspaces (YCQL), select the **Rename** option.

    If you are restoring a YSQL backup to a universe with an existing database with the same name, you must rename the database.

1. If you selected a YCQL backup, you can choose to select specific tables to restore, by selecting the **Select a subset of tables** option.

    Note that this option is only available if the following conditions are met:

    - The backup was made on a universe running YugabyteDB v2.16.0 or later.
    - The selected target universe is running YugabyteDB v2.18.0 or later.

1. If you chose to rename databases/keyspaces or select tables, click **Next** to rename keyspaces and, if applicable, select tables.

1. Click **Restore**.

The restore begins immediately. When finished, a completed **Restore Backup** task appears under **Tasks > Task History**.

To confirm that the restore succeeded, select the **Tables** tab to compare the original table with the table to which you restored.

To view the details of a restored database, navigate to **Universes > Restore History**, or **Backups > Restore History**.

## Restore a PITR-enabled backup

<!-- Restoring a PITR-enabled backup is currently {{<tags/feature/ea>}}.

You can restore entire backups with or without PITR, restore selected entities, and to get a list of the restorable entities using the following APIs.

### List restorable entities

To get a list of restorable entities from a backup, use the following API request:

```sh
curl 'http://<platform-url>/api/v1/customers/:cUUID/backups/:baseBackupUUID/restorable_keyspace_tables'
```

You should see a response similar to the following:

```sh
[
  {
    "tableNames": [
      "test4",
      "test2",
      "test"
    ],
    "keyspace": "test2"
  },
  {
    "tableNames": [
      "test4",
      "test2"
    ],
    "keyspace": "test"
  }
]
```

To check if the selected restore entities can be restored to a specific point in time using the backup's metadata available in YBA database, use the following request API:

```sh
curl 'http://<platform-url>/api/v1/customers/:cUUID/restore/validate_restorable_keyspace_tables' \
-d '{
  "backupUUID": "f136a43f-5b1c-4ef9-ba55-17346d13c65c",
  "restoreToPointInTimeMillis": 1723114010000,
  "keyspaceTables": [
    {
      "keyspace": "test"
    }
  ]
}'
```

You should see a response similar to the following:

```sh
# Returns an error message, if the entities are not restorable in the provided point in time window
{
  "success": false,
  "error": "Some objects cannot be restored",
  "errorJson": [
    {
      "tableNames": [],
      "keyspace": "test3"
    }
  ]
}
```

### Restore an entire backup

The following restore API allows you to restore data from a backup location. You can also use `restoreToPointInTimeMillis` (for PITR-enabled restores) to restore data to a specific point in time.

```sh
curl 'http://<platform-url>/api/v1/customers/:cUUID/restore' \
  -d '{
    "backupStorageInfoList": [
      {
        "storageLocation": "s3://backups.yugabyte.com/test/univ-816ecdcd-8031-4a41-ad62-f49d8a2aa6dc/ybc_backup-b9b3204fd33a4e41b25276fc816fb3b9/incremental/2024-08-08T11:02:56/multi-table-test2_8c42714dbc95469ebb2e31221a851dc1",
        "keyspace": "test5",
        "backupType": "PGSQL_TABLE_TYPE"
      }
    ],
    "universeUUID": "816ecdcd-8031-4a41-ad62-f49d8a2aa6dc",
    "restoreToPointInTimeMillis": 1723113480000,
    "storageConfigUUID": "20946d96-978f-4577-ae28-c156eebb6aad",
    "customerUUID": "f33e3c9b-75ab-4c30-80ad-cba85646ea39"
  }'
```-->

You can restore entire backups with or without PITR. To enable the feature in YugabyteDB Anywhere, set the **Option for Off-Cluster PITR based Backup Schedule** Global Runtime Configuration option (config key `yb.ui.feature_flags.off_cluster_pitr_enabled`) to true. Refer to [Manage runtime configuration settings](../../administer-yugabyte-platform/manage-runtime-config/). Note that only a Super Admin user can modify Global configuration settings.

If you created backups using a [scheduled backup policy with PITR](../schedule-data-backups/), you can restore YugabyteDB universe data from a backup as follows:

1. In the **Backups** list, select the backup to restore to display the **Backup Details**.

1. Select **Restore Entire Backup** to open the **Restore Backup** dialog.

1. Select the Keyspaces/Databases you want to restore. You can choose to backup either All Databases/Keyspaces, or a single database/keyspace.

    ![Restore backup](/images/yp/restore-backup-pitr.png)

1. You can select time to restore to based on the backup time or to an earlier point in time. Select the **An earlier point in time** option to show the available restore window (start and end times) for the restoration.

    For YCQL backups, you can select a subset of tables to restore. If a table is not available in the specified restore window, an error message is displayed.

    Note that you cannot restore to a point in time earlier than the most recent DDL change.

1. When finished, click **Next**.

1. Select the **Target Universe** where you want to restore the backup. You also have the option to rename the keyspaces/databases.

1. If the backup is encrypted, choose the appropriate [KMS configuration](../../security/create-kms-config/aws-kms/).

1. If you are renaming keyspaces/databases, click **Next** and enter the new names.

1. Click **Restore** when you are done.

The restore begins immediately. When finished, a completed **Restore Backup** task appears under **Tasks > Task History**.

To confirm that the restore succeeded, select the **Tables** tab to compare the original table with the table to which you restored.

To view the details of a restored database, navigate to **Universes > Restore History**, or **Backups > Restore History**.

## Advanced restore procedure

In addition to the basic restore, an advanced restore option is available for the following circumstances:

- you have more than one YugabyteDB Anywhere installation and want to restore a database or keyspace from a different YugabyteDB Anywhere installation to the current universe.
- you want to restore a backup that was [moved to a different location](../back-up-universe-data/#moving-backups-between-buckets) (for example, for long-term storage) and is no longer being managed by YugabyteDB Anywhere; that is, is no longer listed in the **Backups** list.

For information regarding components of a backup, refer to [Access backups in storage](../back-up-universe-data/#access-backups-in-storage).

### Prerequisites

To perform an advanced restore, you need the following:

- If the backup had [encryption at rest enabled](../../security/enable-encryption-at-rest), a matching KMS configuration in the target YugabyteDB Anywhere installation so that the backup can be decrypted.
- A matching [storage configuration](../configure-backup-storage/) in the target YugabyteDB Anywhere installation with credentials to access the storage where the backup is located.

    If you are restoring from a backup that was moved to another location, copy the backup to a location with a corresponding storage configuration in YugabyteDB Anywhere.

- The storage address of the database or keyspace backup you want to restore.

    If the backup is on a different YugabyteDB Anywhere installation, you can obtain the location address as follows:

    1. In the **Backups** list, click the backup (row) to display the **Backup Details**.

    1. In the list of databases (YSQL) or keyspaces (YCQL), click **Copy Location** for the database or keyspace you want to restore. If your backup includes incremental backups, to display the databases or keyspaces, click the down arrow for the increment at which you want to restore.

    1. Note the **Storage Config** used by the backup, along with the database or keyspace name.

    To determine the address from the backup folder structure, refer to [Access backups in storage](../back-up-universe-data/#access-backups-in-storage).

### Perform an advanced restore

To perform an advanced restore, on the YugabyteDB Anywhere installation where you want to perform the advanced restore, do the following:

1. On the **Backups** tab of the universe to which you want to restore, click **Advanced** and choose **Advanced Restore** to display the **Advanced Restore** dialog.

    ![Restore advanced](/images/yp/restore-advanced-ycql-2.20.png)

1. Choose the type of API.

1. In the **Backup location** field, paste the location of the backup you are restoring. For example:

    ```output
    s3://user_bucket/some/sub/folders/univ-a85b5b01-6e0b-4a24-b088-478dafff94e4/ybc_backup-92317948b8e444ba150616bf182a061/incremental/20204-01-04T12: 11: 03/multi-table-postgres_40522fc46c69404893392b7d92039b9e
    ```

1. Select the **Backup config** that corresponds to the storage configuration that was used for the backup. The storage could be on Google Cloud, Amazon S3, Azure, or Network File System.

    Note that the storage configuration bucket takes precedence over the bucket specified in the backup location.

    For example, if the storage configuration you select is for the following S3 Bucket:

    ```output
    s3://test_bucket/test
    ```

    And the address provided in the **Backup location** has the following:

    ```output
    s3://user_bucket/test/univ-xyz...
    ```

    YugabyteDB Anywhere looks for the backup in `test_bucket/test`, not `user_bucket/test`.

1. Specify the name of the database or keyspace from which you are performing a restore.

1. To rename databases (YSQL) or keyspaces (YCQL) in the backup before restoring, select the rename option.

1. If the backup involved universes that had [encryption at rest enabled](../../security/enable-encryption-at-rest), then select the KMS configuration to use.

1. If you chose to rename databases/keyspaces, click **Next**, then enter new names for the databases/keyspaces that you want to rename.

1. Click **Restore**.

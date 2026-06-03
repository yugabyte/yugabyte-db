---
title: Restore universe data
headerTitle: Restore universe data
linkTitle: Restore universe data
description: Use YugabyteDB Anywhere to restore data.
headContent: Restore from full or incremental backups
menu:
  v2.20_yugabyte-platform:
    parent: back-up-restore-universes
    identifier: restore-universe-data
    weight: 30
type: docs
---

To access backups from a specific universe, navigate to the universe and choose **Backups**.

To access all universe backups, navigate to **Backups**.

## Prerequisites

- The target universe must have enough nodes to accommodate the restore.
- If the source universe is encrypted, the KMS configuration that was used to encrypt the universe. See [Back up and restore data from an encrypted at rest universe](../../security/enable-encryption-at-rest/#back-up-and-restore-data-from-an-encrypted-at-rest-universe).
- If the source backup has tablespaces, to restore the tablespaces the target universe must have a matching topology; that is, the zones and regions in the target must be the same as the zones and regions in the source.

    If the topology of the target universe does not match, none of the tablespaces are preserved and all their data is written to the primary region. After the restore, you will have to re-add all the tablespaces. For more information on specifying data placement for tables and indexes, refer to [Tablespaces](../../../explore/ysql-language-features/going-beyond-sql/tablespaces/).

- If you are restoring from a Network File System (NFS) storage configuration, the entire backup must be readable by the `yugabyte` user. If `yugabyte` does not have read access, it is possible for restores to fail with a "file does not exist" error message.

- If a universe that spans multiple regions is backed up to NFS using a different volume for each region, all the NFS volumes must be synced to contain the full backup. Each node will require access to the full backup.

    [Tablespaces](../../../explore/ysql-language-features/going-beyond-sql/tablespaces/) are an exception. A node doesn't require access to backed up tablets if that tablet is pinned to another region using a tablespace.

## Restore an entire or incremental backup

You can restore YugabyteDB universe data from a backup as follows:

1. In the **Backups** list, select the backup to restore to display the **Backup Details**.

    ![Restore YCQL](/images/yp/restore-ycql-backup-details.png)

1. Click **Restore Entire Backup**. Alternatively, if your backup includes incremental backups, you can restore a part of an incremental backup chain by selecting an increment from the list in the **Backup Details** view and clicking its **Restore to this point**.

1. In the **Restore Backup** dialog, select the universe (target) to which you want to restore the backup.

1. If you are restoring data from a universe that has, or previously had, [encryption at rest enabled](../../security/enable-encryption-at-rest), then you must select the KMS configuration to use so that the master keys referenced in the metadata file can be retrieved.

    If the universe was previously encrypted at rest, but is not currently, then the retrieved keys assure that any existing files can be decrypted. The retrieved keys are used to build and augment the universe key registry on the restored universe with the required master keys. The universe data files are restored normally afterwards.

1. To rename databases (YSQL) or keyspaces (YCQL), select the **Rename** option.

    If you are restoring a backup to a universe with an existing database of the same name, you must rename the database.

1. If you are restoring data from a universe that has tablespaces, select the **Restore tablespaces and data to their respective regions** option.

    To restore tablespaces, the target universe must have a topology that matches the source.

    You can restore without tablespaces, in which case none of the tablespaces are preserved and all their data is written to the primary region.

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

    Note that the Creation time shown in the dialog indicates the backup job start time, _not_ the time that the particular database was actually snapshotted and backed up. When multiple databases are included in a backup job, each database is snapshotted and backed up in serial order; thus, each individual database's actual snapshot and backup time is not the same as the backup job start time. To restore a database to a specific point in time, use [Point-in-time recovery](../pitr/).

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

## Advanced restore procedure

In addition to the basic restore, an advanced restore option is available for the following circumstances:

- you have more than one YugabyteDB Anywhere installation and want to restore a database or keyspace from a different YugabyteDB Anywhere installation to the current universe.
- you want to restore a backup that was [moved to a different location](../back-up-universe-data/#moving-backups-between-buckets) (for example, for long-term storage) and is no longer being managed by YBA; that is, is no longer listed in the **Backups** list.

For information regarding components of a backup, refer to [Access backups in storage](../back-up-universe-data/#access-backups-in-storage).

### Prerequisites

To perform an advanced restore, you need the following.

#### Storage configuration

A matching [storage configuration](../configure-backup-storage/) in the target YBA installation with credentials to access the storage where the backup is located.

If you are restoring from a backup that was moved to another location, copy the backup to a location with a corresponding storage configuration in YBA.

#### Backup location

The backup location of the database or keyspace backup you want to restore.

The directory structure of the backup location matters. For a restore to succeed, YBA requires the full path; that includes everything after the storage address. For NFS backups, YBA automatically adds the `yugabyte_backup` directory; the backup location path you provide must also include the `yugabyte_backup` directory.

For example, for an S3 storage configuration with the storage address `s3://mybucket/yb-backups/`, a valid location is the following:

```output
s3://mybucket/yb-backups/univ-<universe-name>-<universe-uuid>/<database>/ybc_backup-<uuid>/full/<timestamp>/multi-table-<keyspace>_<uuid>
```

Everything after the address (`s3://mybucket/yb-backups`) is required.

For an NFS storage configuration with the address `/mnt/backup/`, a valid location is the following:

```output
/mnt/backup/yugabyte_backup/univ-<universe-name>-<universe-uuid>/<database>/ybc_backup-<uuid>/full/<timestamp>/multi-table-<keyspace>_<uuid>
```

Everything from the `yugabyte_backup` folder down is required.

If you moved or copied the backup to a new location, make sure the storage configuration has the correct storage address to match the location where the backup data now resides. YBA automatically replaces the storage address of the backup location you provide with the storage address of the storage configuration you select.

For example, if you provide a _storage configuration_ with an address of `s3://newbucket/backups` and the following _backup location_:

```output
s3://mybucket/yb-backups/univ-<universe-name>-<universe-uuid>/<database>/ybc_backup-<uuid>/full/<timestamp>/multi-table-<keyspace>_<uuid>
```

YBA will look in the following location for the backup:

```output
s3://newbucket/backups/univ-<universe-name>-<universe-uuid>/<database>/ybc_backup-<uuid>/full/<timestamp>/multi-table-<keyspace>_<uuid>
```

For more information on the folder structure of YugabyteDB Anywhere backups, refer to [Access backups in storage](../back-up-universe-data/#access-backups-in-storage).

If the backup is still being managed by YBA, you can obtain the address of the backup location, along with the storage configuration and database name, as follows:

1. On the YBA installation where the backup is registered (which may differ from where you are performing the restore), navigate to **Backups** and click the backup (row) to display the **Backup Details**.

1. In the list of databases (YSQL) or keyspaces (YCQL), click **Copy Location** for the database or keyspace you want to restore. (If your backup includes incremental backups, to display the databases or keyspaces, click the down arrow for the increment at which you want to restore.)

1. Note the **Storage Config** used by the backup, along with the database or keyspace name.

#### KMS configuration

If the backup had [encryption at rest enabled](../../security/enable-encryption-at-rest), you need a matching KMS configuration in the target YBA installation so that the backup can be decrypted.

### Perform an advanced restore

To perform an advanced restore, on the YugabyteDB Anywhere installation where you want to perform the advanced restore, do the following:

1. On the **Backups** tab of the universe to which you want to restore, click **Advanced** and choose **Advanced Restore** to display the **Advanced Restore** dialog.

    ![Restore advanced](/images/yp/restore-advanced-ycql-2.20.png)

1. Choose the type of API.

1. In the **Backup location** field, paste the [backup location](#backup-location) of the backup you are restoring.

    For example:

    ```output
    s3://user_bucket/some/sub/folders/univ-myuniverse-a85b5b01-6e0b-4a24-b088-478dafff94e4/ybc_backup-92317948b8e444ba150616bf182a061/incremental/2024-01-04T12:11:03/multi-table-postgres_40522fc46c69404893392b7d92039b9e
    ```

1. Select the **Backup config** that corresponds to the [storage configuration](#storage-configuration) that was used for the backup. The storage could be on Google Cloud, Amazon S3, Azure, or Network File System.

    The backup config bucket takes precedence over the bucket specified in the backup location.

    For example, if the backup config you select is for the following S3 bucket:

    ```output
    s3://test_bucket/test
    ```

    And the address provided in the **Backup location** has the following:

    ```output
    s3://user_bucket/test/univ-xyz...
    ```

    YBA looks for the backup in `test_bucket/test`, not `user_bucket/test`.

1. Specify the name of the database or keyspace from which you are performing a restore.

1. To rename databases (YSQL) or keyspaces (YCQL) in the backup before restoring, select the rename option.

1. If the backup involved universes that had [encryption at rest enabled](../../security/enable-encryption-at-rest), then select the KMS configuration to use.

1. If you chose to rename databases/keyspaces, click **Next**, then enter new names for the databases/keyspaces that you want to rename.

1. Click **Restore**.

---
title: Restore universe YCQL data
headerTitle: Restore universe YCQL data
linkTitle: Restore universe data
description: Use YugabyteDB Anywhere to restore data in YCQL tables.
menu:
  v2.16_yugabyte-platform:
    parent: back-up-restore-universes
    identifier: restore-universe-data-2-ycql
    weight: 30
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="{{< relref "./ysql.md" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="{{< relref "./ycql.md" >}}" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

## Basic restore procedure

You can restore YugabyteDB universe YCQL data from a backup as follows:

1. Open your universe and select **Backups**.

2. If you want to restore a backup from a specific keyspace, click on the backup and use its **Backup Details** page to perform the restore procedure.

3. If you want to restore a full backup, use the **Backups** page to select the backup and click its **... > Restore Entire Backup**, as per the following illustration:

    ![Restore backup](/images/yp/restore-entire-backup-ycql.png)

4. Complete the fields of the **Restore Backup** dialog shown in the following illustration:

    ![Restore backup - YCQL](/images/yp/restore-universe-data-ycql-1.png)

    - Select the name of the universe to which you want to restore the backup.

    - Optionally and depending on your cloud provider, if you are restoring data from a universe that has, or previously had, [encryption at rest enabled](../../../security/enable-encryption-at-rest), then you must select the KMS configuration to use so that the universe keys referenced in the metadata file can be retrieved. If the universe was previously encrypted at rest, but is not currently, then the retrieved keys assure that any existing files can be decrypted. The retrieved keys are used to build and augment the key registry on the restore universe with the required universe keys. The universe data files are restored normally afterwards.

    - Optionally, specify the number of parallel threads that are allowed to run. This can be any number between `1` and `100`.

    - Select **Rename databases in this backup before restoring** and then click **Next: Rename Database/Keyspaces**.

    - Specify the new name for a keyspace or database in the backup and click **Restore**, as per the following illustration:

      ![Restore backup - YCQL](/images/yp/restore-universe-data-ycql-2.png)

      The restore begins immediately. When finished, a completed **Restore Backup** task appears under **Tasks > Task History**.

5. To confirm that the restore succeeded, select the **Tables** tab to compare the original table with the table to which you restored.

## Advanced restore procedure

In addition to the basic restore, an advanced option is available if you have more than one YugabyteDB Anywhere installation and want to restore a database or keyspace from a different YugabyteDB Anywhere installation to the current universe.

To perform this type of restore, click **... > Advanced Restore**, as per the following illustration:

![Restore advanced](/images/yp/restore-advanced-ycql-1.png)

To proceed, complete the fields of the **Advanced Restore** dialog shown in the following illustration:

![Restore advanced](/images/yp/restore-advanced-ycql.png)

- Select YCQL as the type of API.

- Specify the location of the backup you want to restore.

- Select the cloud provider-specific configuration of the backup storage. The storage could be on Google Cloud, Amazon S3, Azure, or Network File System.

- Specify the name of the database from which you are performing a restore.

- Optionally, specify the number of parallel threads that are allowed to run. This can be any number between 1 and 100.

- Optionally, if the backup involved universes that had [encryption at rest enabled](/preview/yugabyte-platform/security/enable-encryption-at-rest), then select the KMS configuration to use.

- If you do not select **Rename databases in this backup before restoring**, then click **Restore** to start the restore process immediately.

  If you select **Rename databases in this backup before restoring**, then click **Next: Rename Database/Keyspaces**, specify the new name for a keyspace or database in the backup and click **Restore**.

You can access a list of all backups from all universes by navigating to **Backups** on the YugabyteDB Anywhere left-side menu, as per the following illustration:

![Backups](/images/yp/backups-list.png)

By clicking on a specific universe included in the list, you can access the backup details and trigger a restore.
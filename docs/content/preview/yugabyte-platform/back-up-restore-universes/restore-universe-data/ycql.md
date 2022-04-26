---
title: Restore universe YCQL data
headerTitle: Restore universe YCQL data
linkTitle: Restore universe data
description: Use YugabyteDB Anywhere to restore data in YCQL tables.
menu:
  preview:
    parent: back-up-restore-universes
    identifier: restore-universe-data-2-ycql
    weight: 30
isTocNested: true
showAsideToc: true
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

You can restore YugabyteDB universe YCQL data from a backup as follows:

1. Open your universe and then select **Backups**.

2. Click **Restore Backup** to open the **Restore data to** dialog shown in the following illustration:

    <br/><br/>
    ![Restore backup - YCQL](/images/yp/restore-universe-data-ycql.png)<br><br>

3. Enter the following information:

    - **Storage** Select the storage configuration type: `GCS Storage`, `S3 Storage`, or `NFS Storage`.
    - **Storage Location**: Specify the storage location.
    - **Universe**: Select the YCQL universe to restore.
    - **Keyspace**: Specify the keyspace.
    - **Table**: Specify the table to be restored. Note: The table name must be different than the backed up table name.
    - **Parallel Threads**: Default is `8`. This value can be changed to a value between `1` and `100`.
    - **KMS Configuration**: Optionally, if you are restoring data from a universe that has, or previously had, [encryption at rest enabled](../../../security/enable-encryption-at-rest), then you must select the KMS configuration to use so that the universe keys referenced in the metadata file can be retrieved. If the universe was previously encrypted at rest, but is not currently, then the retrieved keys assure that any existing files can be decrypted. The retrieved keys are used to build and augment the key registry on the restore universe with the required universe keys. The universe data files are restored as normal afterwards.

4. Click **OK**.<br>

    The restore begins immediately. When finished, a completed **Restore Backup** task appears in the **Tasks** tab.

5. To confirm that the restore succeeded, select the **Tables** tab to compare the original table with the table to which you restored, as per the following illustration:

  ![Tables View](/images/yp/tables-view-ycql.png)

---
title: Back up universe YSQL data
headerTitle: Back up universe YSQL data
linkTitle: Back up universe data
description: Use YugabyteDB Anywhere to back up data in YSQL tables.
aliases:
  - /preview/manage/enterprise-edition/backup-restore
  - /preview/manage/enterprise-edition/back-up-restore-data
  - /preview/yugabyte-platform/manage/backup-restore-data
  - /preview/yugabyte-platform/back-up-restore-universes/back-up-universe-data/
menu:
  preview_yugabyte-platform:
    parent: back-up-restore-universes
    identifier: back-up-universe-data-1-ysql
    weight: 20
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="{{< relref "./ysql.md" >}}" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="{{< relref "./ycql.md" >}}" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

<br>You can use YugabyteDB Anywhere to back up your YugabyteDB universe YSQL data.

To view, [restore](../../restore-universe-data/ysql/), or delete existing backups for your universe, navigate to that universe and select **Backups**, as per the following illustration:

![Create Backup](/images/yp/create-backup-new-ysql.png)

By default, the list displays all the backups generated for the universe regardless of the time period. You can configure the list to only display the backups created during a specific time period, such as last year,  last month, and so on. In addition, you can specify a custom time period.

## Create backups

The **Backups** page allows you to create new backups that start immediately, as follows:

- Click **Backup now** to open the dialog shown in the following illustration:<br>

  ![Backup](/images/yp/create-backup-new-2.png)

- In the **Backup Now** dialog, select YSQL as the API type and then complete all the other fields.

  Notice that the contents of the **Select the storage config you want to use for your backup** field list depends on your existing backup storage configurations. For more information, see [Configure backup storage](../../configure-backup-storage/).

- Click **Backup**.

If the universe has [encryption at rest enabled](../../../security/enable-encryption-at-rest), data files are backed up as-is (encrypted) to reduce the computation cost of a backup and to keep the files encrypted. A universe key metadata file, containing key references, is also backed up. To allow YugabyteDB Anywhere to back up your data with the user authentication enabled, follow the instructions provided in [Edit configuration flags](../../../manage-deployments/edit-config-flags) to add the `ysql_enable_auth=true` and `ysql_hba_conf_csv="local all all trust"` YB-TServer flags.

<!-- 

{{< note title="Note" >}}

Versions of YugabyteDB Anywhere prior to 2.11.2.0 do not support backups of YSQL databases that use `enum` types. To mitigate the issue, it is recommended that you use the `ysql_dump` utility in combination with the `/COPY` action as a workaround.

{{< /note >}}

-->

<!-- The preceding note should say 2.11.2.0. Careful with search and replace on version numbers! -->

For information on how to schedule backups for a later time or as a recurring task, see [Schedule universe YSQL data backups](../../schedule-data-backups/ysql/).

To view detailed information about an existing backup, click on it to open **Backup Details**. In addition to actions such as deleting and restoring the backup, as well as restoring and copying the database location, you can use **Backup Details** to add an [incremental backup](#create-incremental-backup) for universes that had the YB Controller automatically installed during their creation. The same universes allow you to configure their throttle parameters by clicking **... > Configure Throttle Parameters**, as per the following illustration:

![Throttle parameters](/images/yp/backup-throttle-config-button.png)

You define throttle parameters to enhance your universe's backups and restore performance using the **Configure Throttle Parameters** dialog shown in the following illustration:

![Throttle](/images/yp/backup-restore-throttle.png)

To access a list of all backups from all universes, including the deleted universes, navigate to **Backups** on the YugabyteDB Anywhere left-side menu, as per the following illustration:

![Backups](/images/yp/backups-list.png)

## Create incremental backups

Incremental backups are taken on top of a complete backup. To reduce the length of time spent on each backup, only SST files that are new to YugabyteDB and not present in the previous backups are incrementally backed up. For example, in most cases, for incremental backups occurring every hour, the 1-hour delta would be significantly smaller compared to the complete backup. The restore happens until the point of the defined increment.

You can create an incremental backup on any complete or incremental backup taken using YB-Controller, as follows:

- Navigate to **Backups**, select a backup, and then click on it to open **Backup Details**.

- In the  **Backup Details** view, click **Add Incremental Backup**.

- On the **Add Incremental Backup** dialog, click **Confirm**.


A successful incremental backup appears in the list of backups. 

You can delete only the full backup chain which includes a complete backup and its incremental backups. You cannot delete a subset of successful incremental bakups.

A failed incremental backup, which you can delete, is reported similarly to any other failed backup operations.
---
title: Back up universe YCQL data
headerTitle: Back up universe YCQL data
linkTitle: Back up universe data
description: Use YugabyteDB Anywhere to back up data in YCQL tables.
menu:
  v2.14_yugabyte-platform:
    parent: back-up-restore-universes
    identifier: back-up-universe-data-2-ycql
    weight: 20
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

You can use YugabyteDB Anywhere to back up your YugabyteDB universe YCQL data.

{{< note title="Note" >}}

Non-transactional backups are not supported.

{{< /note >}}

To view, [restore](../../restore-universe-data/ycql/), or delete existing backups for your universe, navigate to that universe and select **Backups**, as per the following illustration:

![Create Backup](/images/yp/create-backup-new-1.png)

By default, the list displays all the backups generated for the universe regardless of the time period. You can configure the list to only display the backups created during a specific time period, such as last year,  last month, and so on. In addition, you can specify a custom time period.

To view detailed information about an existing backup, click on it to open **Backup Details**.

The **Backups** page allows you to create new backups that start immediately, as follows:

- Click **Backup now** to open the dialog shown in the following illustration:<br><br>

  ![Backup](/images/yp/create-backup-new-3.png)<br><br>

- In the **Backup Now** dialog, select YCQL as the API type.

- Complete the **Select the storage config you want to use for your backup** field whose list depends on your existing backup storage configurations. For more information, see [Configure backup storage](../../configure-backup-storage/).

- Select the database to back up.

- Specify whether you want to back up all tables in the keyspace to which the database belongs or only  certain tables. If you choose **Select a subset of tables**, a **Select Tables** dialog opens allowing you to select one or more tables to back up.

- Specify the period of time during which the backup is to be retained. Note that there's an option to never delete the backup.

- Optionally, specify the number of threads that should be available for the backup process.

- Click **Backup**.

If the universe has [encryption at rest enabled](../../../security/enable-encryption-at-rest), data files are backed up as-is (encrypted) to reduce the computation cost of a backup and to keep the files encrypted. A universe key metadata file containing key references is also backed up.

For information on how to schedule backups for a later time or as a recurring task, see [Schedule universe YCQL data backups](../../schedule-data-backups/ycql/).

To access a list of all backups from all universes, including the deleted universes, navigate to **Backups** on the YugabyteDB Anywhere left-side menu.

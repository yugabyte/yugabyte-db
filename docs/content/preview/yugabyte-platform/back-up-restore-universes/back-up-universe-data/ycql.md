---
title: Back up universe YCQL data
headerTitle: Back up universe YCQL data
linkTitle: Back up universe data
description: Use YugabyteDB Anywhere to back up data in YCQL tables.
menu:
  preview:
    parent: back-up-restore-universes
    identifier: back-up-universe-data-2-ycql
    weight: 20
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

You can use YugabyteDB Anywhere to back up your YugabyteDB universe YCQL data.

To view existing backups, as well as restore or delete them, navigate to your universe and select **Backups**, as per the following illustration:

![Create Backup](/images/yp/create-backup-new-1.png)

The **Backups** page allows you to create new backups that start immediately, as follows: 

- Select the time period for the backup and click **Backup now** to open the dialog shown in the following illustration:<br><br>

  ![Backup](/images/yp/create-backup-new-3.png)<br><br>

- In the **Backup Now** dialog, select YCQL as the API type and then complete all the other fields.

  Notice that the contents of the **Select the storage config you want to use for your backup** field list depends on your existing backup storage configurations. For more information, see [Configure backup storage](../../configure-backup-storage/).

- Click **Backup**.

If the universe has [encryption at rest enabled](../../../security/enable-encryption-at-rest), data files are backed up as-is (encrypted) to reduce the computation cost of a backup and to keep the files encrypted. A universe key metadata file containing key references is also backed up.

For information on how to schedule backups for a later time or as a recurring task, see [Schedule universe YCQL data backups](../../schedule-data-backups/ycql/).
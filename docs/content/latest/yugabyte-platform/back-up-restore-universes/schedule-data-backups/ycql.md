---
title: Schedule universe YCQL data backups
headerTitle: Schedule universe YCQL data backups
linkTitle: Schedule data backups
description: Use Yugabyte Platform to create a scheduled backup of universe YCQL data.
aliases:
  - /latest/manage/enterprise-edition/schedule-backups/
  - /latest/manage/enterprise-edition/schedule-data-backup/
menu:
  latest:
    identifier: schedule-data-backups-2-ycql
    parent: back-up-restore-universes
    weight: 40
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

Use Yugabyte Platform to perform a regularly scheduled backup of a full YugabyteDB universe or selected tables.

## Schedule a backup

1. In the Yugabyte Platform console, click **Universes** in the navigation bar, then click the name of the universe you want to schedule backups for.
2. Click the **Tables** tab and verify that backups are enabled. If disabled, click **Enable Backup**.
3. Click the **Backups** tab and then click **Create Scheduled Backup**. The **Create Backup** form appears.

![Create Backup form](/images/yp/create-backup.png)

4. Enter the **Backup frequency** (an interval in in milliseconds) or enter a `cron` expression.
5. Select the **Storage** option.
6. Select the **Table keyspace**.
7. In the **Tables to backup** field, select specific tables or the **Full Universe Backup** option.
8. Click **OK** to create the schedule. The first backup will begin immediately and then subsequent backups will be created based on the backup frequency.

## Disable a scheduled backup

If required, you can temporarily disable scheduled backups by going to the **Tables** tab in a universe and click **Disable Backups**.

## Delete a schedule

If you need to remove a scheduled backup, go to the **Backups** tab for your universe and for the scheduled backup, click **Options** and then click **Delete schedule**. The scheduled backup is deleted.

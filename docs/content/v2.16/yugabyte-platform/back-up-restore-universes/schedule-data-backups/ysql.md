---
title: Schedule universe YSQL data backups
headerTitle: Schedule universe YSQL data backups
linkTitle: Schedule data backups
description: Use YugabyteDB Anywhere to create scheduled backups of universe YSQL data.
menu:
  v2.16_yugabyte-platform:
    identifier: schedule-data-backups-1-ysql
    parent: back-up-restore-universes
    weight: 15
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

You can use YugabyteDB Anywhere to perform regularly scheduled backups of YugabyteDB universe data for all YSQL tables in a namespace.

To back up your universe YSQL data immediately, see [Back up universe YSQL data](../../back-up-universe-data/ysql/).

## Create a scheduled backup policy

Before scheduling a backup of your universe YSQL data, create a policy, as follows:

1. Navigate to **Universes**.

1. Select the name of the universe for which you want to schedule backups.

1. Select the **Tables** tab and click **Actions** to verify that backups are enabled. If disabled, click **Enable Backup**.

1. Select the **Backups** tab and then select **Scheduled Backup Policies**.

1. Click **Create Scheduled Backup Policy**.

    <!--    ![Create Backup form](/images/yp/scheduled-backup-ysql-1.png)-->

1. Provide the backup policy name.

1. Specify the interval between backups or select **Use cron expression (UTC)**.

1. Select the backup storage configuration. Notice that the contents of the **Select the storage config you want to use for your backup** list depends on your existing backup storage configurations. For more information, see [Configure backup storage](../../configure-backup-storage/).

1. Select the database to backup. You may also choose to back up all databases associated with your universe.

1. Specify the period of time during which the backup is to be retained. Note that there's an option to never delete the backup.

1. Specify the number of threads that should be available for the backup process.

1. Click **Create**.

Subsequent backups are created based on the value you specified for **Set backup intervals** or **Use cron expression**.

## Disable backups

You can disable all backups, including scheduled ones, as follows:

1. Navigate to the universe's **Tables** tab.
1. Click **Actions > Disable Backup**.

<!--

## Delete a scheduled backup

You can permanently remove a scheduled backup, as follows:

1. Navigate to the universe's **Backups** tab.

2. Find the scheduled backup and click **... > Delete Schedule**.

   -->

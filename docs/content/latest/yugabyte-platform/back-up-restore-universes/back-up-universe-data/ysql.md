---
title: Back up data universe YSQL data
headerTitle: Back up universe YSQL data
linkTitle: Back up universe data
description: Use Yugabyte Platform to back up data in YSQL tables.
aliases:
  - /latest/manage/enterprise-edition/backup-restore
  - /latest/manage/enterprise-edition/back-up-restore-data
  - /latest/yugabyte-platform/manage/backup-restore-data
  - /latest/yugabyte-platform/back-up-restore-universes/back-up-universe-data/
menu:
  latest:
    parent: back-up-restore-universes
    identifier: back-up-universe-data-1-ysql
    weight: 20
isTocNested: true
showAsideToc: true
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

Follow the steps here to use the Yugabyte Platform to back up YugabyteDB universe data.

1. Go to the **Backups** tab and then click **Create Backup**. A modal should appear where you can
enter the table and select your backup options. 

![Create Backup - YSQL](/images/yp/create-backup-ysql.png)

Click **OK**. If you refresh the page, you'll eventually see a completed task.

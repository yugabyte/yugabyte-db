---
title: Back up universes
headerTitle: Back up and restore universes
linkTitle: Back up universes
description: Use YugabyteDB Anywhere to back up and restore YugabyteDB universe data.
image: /images/section_icons/manage/backup.png
headcontent: Use YugabyteDB Anywhere to back up and restore YugabyteDB universes and data.
menu:
  v2.16_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: back-up-restore-universes
weight: 645
type: indexpage
---

You can use YugabyteDB to schedule and manage backups of your universe data. This includes the following features:

- On-demand [backup](back-up-universe-data/ysql/) and [restore](restore-universe-data/ysql/).
- [Scheduled backups](schedule-data-backups/ysql/). Schedule backups at regular intervals, along with retention periods.
- [Incremental backups](back-up-universe-data/ysql/#create-incremental-backups). Create a schedule to take full backups periodically and incremental backups between those full backups.
- [Configurable performance parameters](back-up-universe-data/ysql/#configure-backup-performance-parameters). Tune parallelization and buffers for faster backup and restore performance. In most cases, this results in 5x or more speed improvements in backups and restores.

{{< note title="Note" >}}
Configurable performance parameters and incremental backups are mediated using the yb-controller process, which is only available in YBA 2.16 or later for universes with database version 2.16 or later.
{{< /note >}}

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="configure-backup-storage/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Configure backup storage</div>
      </div>
      <div class="body">
        Use YugabyteDB Anywhere to configure the storage location for your universe data.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="back-up-universe-data/ysql/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Back up universe data</div>
      </div>
      <div class="body">
        Use YugabyteDB Anywhere to back up universe data.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="restore-universe-data/ysql/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Restore universe data</div>
      </div>
      <div class="body">
        Use YugabyteDB Anywhere to restore universe data.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="schedule-data-backups/ysql/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/high_performance.png" aria-hidden="true" />
        <div class="title">Schedule universe data backups</div>
      </div>
      <div class="body">
        Use YugabyteDB Anywhere to schedule backups of universe data.
      </div>
    </a>
  </div>

</div>

---
title: Back up and restore universes
headerTitle: Back up and restore universes
linkTitle: Back up universes
description: Use YugabyteDB Anywhere to back up and restore YugabyteDB universe data.
image: /images/section_icons/manage/backup.png
headcontent: Use YugabyteDB Anywhere to back up and restore YugabyteDB universes and data
menu:
  stable_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: back-up-restore-universes
weight: 640
type: indexpage
---

You can use YugabyteDB to schedule and manage backups of your universe data. This includes the following features:

- On-demand [backup](back-up-universe-data/) and [restore](restore-universe-data/).
- [Scheduled backups](schedule-data-backups/). Schedule backups at regular intervals, along with retention periods.
- [Incremental backups](back-up-universe-data/#create-incremental-backups). Create a schedule to take full backups periodically and incremental backups between those full backups.
- [Configurable performance parameters](back-up-universe-data/#configure-backup-performance-parameters). Tune parallelization and buffers for faster backup and restore performance. In most cases, this results in 5x or more speed improvements in backups and restores.
- [Point-in-time recovery](pitr/). Recover universe data from a specific point in time.
- [Flexible storage](configure-backup-storage/). Store backups in the cloud or in your data center.

{{< note title="Note" >}}
Configurable performance parameters and incremental backups are mediated using the yb-controller process, which is only available in YBA 2.16 or later for universes with YugabyteDB version 2.16 or later.
{{< /note >}}

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="configure-backup-storage/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Configure backup storage</div>
      </div>
      <div class="body">
        Configure the storage location for your backups.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="back-up-universe-data/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Back up universe data</div>
      </div>
      <div class="body">
        Back up universes and create incremental backups.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="restore-universe-data/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Restore universe data</div>
      </div>
      <div class="body">
        Restore from full and incremental backups.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="schedule-data-backups/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/high_performance.png" aria-hidden="true" />
        <div class="title">Schedule universe data backups</div>
      </div>
      <div class="body">
        Create backup schedules to regularly back up universe data.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="pitr/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/high_performance.png" aria-hidden="true" />
        <div class="title">Perform point-in-time recovery</div>
      </div>
      <div class="body">
        Recover universe data from a specific point in time.
      </div>
    </a>
  </div>

</div>

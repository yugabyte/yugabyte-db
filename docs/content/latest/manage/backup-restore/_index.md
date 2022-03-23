---
title: Back up and restore data
headerTitle: Back up and restore
linkTitle: Back up and restore
description: Back up and restore YugabyteDB.
image: /images/section_icons/manage/enterprise.png
headcontent: Create backups and restore your data.
aliases:
  - /manage/backup-restore/
menu:
  latest:
    identifier: backup-restore
    parent: manage
    weight: 702
---

Backup and Recovery is the process of creating and storing copies of the data that can be used for protection against data loss. By setting up a proper data backup strategy, you make sure you can always restore to a most recent known working state and minimize application downtimes, which in turn guarantees business and application continuity.

Note that unlike traditional single-instance databases, YugabyteDB is designed for fault tolerance. By maintaining at least three copies of the data across multiple data regions or multiple clouds, it makes sure no losses occur in case a single node or a single data region becomes unavailable. Thus, with YugabyteDB, backups are mainly used to:
1. Recover from a user or software error, e.g. accidental table removal.
2. Recover from a severe disaster scenario, like a full cluster failure or a simultaneous outage of multiple data regions (probability of such scenarios is extremely low, but it’s still recommended to maintain a way to recover from them).
3. Maintain a remote copy of the data as required by data protection regulations.

<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="export-import-data/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Export and import data</div>
      </div>
      <div class="body">
        This section describes how to export to and import data from a SQL or CQL script.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="snapshot-ysql/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Distributed snapshot and restore data</div>
      </div>
      <div class="body">
        This section describes how to back up and restore data using distributed snapshots.
      </div>
    </a>
  </div>
</div>
<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="point-in-time-recovery/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/create_universe.png" aria-hidden="true" />
        <div class="title">Point-in-time recovery</div>
      </div>
      <div class="body">
        This section describes how to restore data from a particular point in time.
      </div>
    </a>
  </div>
</div>

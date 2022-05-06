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
  preview:
    identifier: backup-restore
    parent: manage
    weight: 702
---

Backup and recovery is the process of creating and storing copies of your data for protection against data loss. With a proper backup strategy, you can always restore to a most-recent known working state and minimize application downtime. This in turn guarantees business and application continuity.

Unlike traditional single-instance databases, YugabyteDB is designed for fault tolerance. By maintaining at least three copies of your data across multiple data regions or multiple clouds, it makes sure no losses occur if a single node or single data region becomes unavailable. With YugabyteDB, you mainly use backups to:

* Recover from a user or software error, such as accidental table removal.
* Recover from a disaster, like a full cluster failure or a simultaneous outage of multiple data regions. (Such scenarios are extremely unlikely, but you should still maintain a way to recover from them.)
* Maintain a remote copy of data, as required by data-protection regulations.

<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="export-import-data/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Export and import data</div>
      </div>
      <div class="body">
        Export and import data using SQL or CQL scripts.
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
        Back up and restore data using distributed snapshots.
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
        Restore data from a particular point in time.
      </div>
    </a>
  </div>
</div>

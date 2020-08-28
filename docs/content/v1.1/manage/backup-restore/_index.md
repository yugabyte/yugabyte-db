---
title: Backup and Restore
linkTitle: Backup and Restore
description: Backup and Restore
image: /images/section_icons/manage/enterprise.png
headcontent: Backup and restore your data in YugabyteDB.
block_indexing: true
menu:
  v1.1:
    identifier: manage-backup-restore
    parent: manage
    weight: 702
---

YugabyteDB is a distributed database that internally replicates data. It is possible to place the regions in separate fault domains, therefore backups for the purpose of data redundancy is not necessary. However, it is an operational best practice to have a backup strategy. For example, an error in the application layer could cause it to write bad data into the database. In such a scenario, it is essential to be able to restore the database from a backup to a state before the application error was introduced.

This section goes into details of backing up data and restoring it from YugabyteDB.

<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="backing-up-data/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Backing Up Data</div>
      </div>
      <div class="body">
        This section describes how to create a backup of the data in YugabyteDB.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="restoring-data/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/create_universe.png" aria-hidden="true" />
        <div class="title">Restoring Data</div>
      </div>
      <div class="body">
        This section describes how to restore data into YugabyteDB from a backup.
      </div>
    </a>
  </div>
</div>

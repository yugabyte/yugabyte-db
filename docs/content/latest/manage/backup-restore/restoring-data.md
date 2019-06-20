---
title: Restoring Data
linkTitle: Restoring Data
description: Restoring Data
image: /images/section_icons/manage/enterprise.png
headcontent: Restoring data in YugaByte DB.
aliases:
  - /manage/backup-restore/restoring-data
menu:
  latest:
    identifier: manage-backup-restore-restoring-data
    parent: manage-backup-restore
    weight: 704
---

This page covers how to restore data from a backup. In order to create a backup, refer the section on [backing up data](/manage/backup-restore/backing-up-data/).

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#cassandra" class="nav-link active" id="cassandra-tab" data-toggle="tab" role="tab" aria-controls="cassandra" aria-selected="true">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="cassandra" class="tab-pane fade show active" role="tabpanel" aria-labelledby="cassandra-tab">
    {{% includeMarkdown "ycql/restoring-data.md" /%}}
  </div>
</div>


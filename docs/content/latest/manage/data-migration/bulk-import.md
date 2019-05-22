---
title: Bulk Import
linkTitle: Bulk Import
description: Bulk Import
image: /images/section_icons/manage/enterprise.png
headcontent: Bulk import data into YugaByte DB.
menu:
  latest:
    identifier: manage-bulk-import
    parent: manage-bulk-import-export
    weight: 704
---


Depending on the data volume imported, various bulk import tools can be used to bring data into YugaByte DB.

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#ycql" class="nav-link active" id="ycql-tab" data-toggle="tab" role="tab" aria-controls="ycql" aria-selected="true">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="ycql" class="tab-pane fade show active" role="tabpanel" aria-labelledby="ycql-tab">
    {{% includeMarkdown "ycql/bulk-import.md" /%}}
  </div>
</div>

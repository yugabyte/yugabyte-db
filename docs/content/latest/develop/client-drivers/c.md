---
title: C
linkTitle: C
description: Develop C Apps
aliases:
  - /develop/client-drivers/c/
menu:
  latest:
    identifier: client-drivers-c
    parent: client-drivers
    weight: 550
---

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#ysql" class="nav-link active" id="ysql-tab" data-toggle="tab" role="tab" aria-controls="ysql" aria-selected="false">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li>
    <a href="#cql" class="nav-link" id="cql-tab" data-toggle="tab" role="tab" aria-controls="cql" aria-selected="true">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="ysql" class="tab-pane fade show active" role="tabpanel" aria-labelledby="ysql-tab">
    {{% includeMarkdown "ysql/c.md" /%}}
  </div>
  <div id="cql" class="tab-pane fade" role="tabpanel" aria-labelledby="cql-tab">
    {{% includeMarkdown "ycql/cpp.md" /%}}
  </div>
</div>


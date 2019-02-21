---
title: Java
linkTitle: Java
description: Develop Java Apps
aliases:
  - /develop/client-drivers/java/
menu:
  latest:
    identifier: client-drivers-java
    parent: client-drivers
    weight: 554
---

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#cql" class="nav-link active" id="cql-tab" data-toggle="tab" role="tab" aria-controls="cql" aria-selected="true">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
  <li>
    <a href="#redis" class="nav-link" id="redis-tab" data-toggle="tab" role="tab" aria-controls="redis" aria-selected="false">
      <i class="icon-redis" aria-hidden="true"></i>
      YEDIS
    </a>
  </li>
  <li>
    <a href="#ysql" class="nav-link" id="ysql-tab" data-toggle="tab" role="tab" aria-controls="ysql" aria-selected="false">
      <i class="icon-ysql" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="cql" class="tab-pane fade show active" role="tabpanel" aria-labelledby="cql-tab">
    {{% includeMarkdown "cassandra/java.md" /%}}
  </div>
  <div id="redis" class="tab-pane fade" role="tabpanel" aria-labelledby="redis-tab">
    {{% includeMarkdown "redis/java.md" /%}}
  </div>
  <div id="ysql" class="tab-pane fade" role="tabpanel" aria-labelledby="ysql-tab">
    {{% includeMarkdown "ysql/java.md" /%}}
  </div>
</div>

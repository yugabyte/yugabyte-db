---
title: Develop Apache Spark Apps
linkTitle: Develop Apache Spark Apps
aliases:
  - /develop/client-drivers/spark/
---

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#cql" class="nav-link active" id="cql-tab" data-toggle="tab" role="tab" aria-controls="cql" aria-selected="true">
      <i class="icon-cassandra" aria-hidden="true"></i>
      Cassandra
    </a>
  </li>
  <li >
    <a href="#redis" class="nav-link" id="redis-tab" data-toggle="tab" role="tab" aria-controls="redis" aria-selected="false">
      <i class="icon-redis" aria-hidden="true"></i>
      Redis
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="cql" class="tab-pane fade show active" role="tabpanel" aria-labelledby="cql-tab">
    {{% includeMarkdown "cassandra/spark.md" /%}}
  </div>
  <div id="redis" class="tab-pane fade" role="tabpanel" aria-labelledby="redis-tab">
    {{% includeMarkdown "redis/spark.md" /%}}
  </div>
</div>

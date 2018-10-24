---
title: Aggregations
linkTitle: 4. Aggregations
description: Aggregations
menu:
  latest:
    identifier: pgsql-agg
    parent: explore-pgsql
    weight: 294
---

Aggregates functions are supported. However, the computation of these aggregations is not yet pushed down to the individual nodes. This is a performance optimization that we expect to be available in future beta releases.

If you haven't installed YugaByte DB yet, do so first by following the [Quick Start](../../../quick-start/install/) guide.

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#macos" class="nav-link active" id="macos-tab" data-toggle="tab" role="tab" aria-controls="macos" aria-selected="true">
      <i class="fa fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="#linux" class="nav-link" id="linux-tab" data-toggle="tab" role="tab" aria-controls="linux" aria-selected="false">
      <i class="fa fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>
  <li>
    <a href="#docker" class="nav-link" id="docker-tab" data-toggle="tab" role="tab" aria-controls="docker" aria-selected="false">
      <i class="icon-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="macos" class="tab-pane fade show active" role="tabpanel" aria-labelledby="macos-tab">
    {{% includeMarkdown "binary/aggregations.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade" role="tabpanel" aria-labelledby="linux-tab">
    {{% includeMarkdown "binary/aggregations.md" /%}}
  </div>
   <div id="docker" class="tab-pane fade" role="tabpanel" aria-labelledby="docker-tab">
    {{% includeMarkdown "docker/aggregations.md" /%}}
  </div>
</div>

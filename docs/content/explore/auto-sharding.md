---
title: Auto Sharding
---

YugaByte DB automatically shards your data for you internally. Read more about [how sharding works](/architecture/concepts/sharding/) in YugaByte. By default, YugaByte creates 8 tablets per node in the cluster for each table. In this tutorial, we will explore how automatic sharding is done internally for both CQL as well as Redis tables. We will create these tables with a replication factor = 1 to make it easier to understand how automatic sharding is achieved. We will then insert some values and see how the data gets distributed across the various nodes.

If you haven't installed YugaByte DB yet, do so first by following the [Quick Start](/quick-start/install/) guide.

<ul class="nav nav-tabs">
  <li class="active">
    <a data-toggle="tab" href="#docker">
      <i class="icon-docker"></i>
      Docker
    </a>
  </li>
  <li >
    <a data-toggle="tab" href="#macos">
      <i class="fa fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a data-toggle="tab" href="#linux">
      <i class="fa fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="docker" class="tab-pane fade in active">
    {{% includeMarkdown "/explore/docker/auto-sharding.md" /%}}
  </div>
  <div id="macos" class="tab-pane fade">
    {{% includeMarkdown "/explore/binary/auto-sharding.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade">
    {{% includeMarkdown "/explore/binary/auto-sharding.md" /%}}
  </div> 
</div>

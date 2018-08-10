---
title: Auto Rebalancing
linkTitle: 3. Auto Rebalancing
description: Auto Rebalancing
aliases:
  - /explore/auto-rebalancing/
  - /latest/explore/auto-rebalancing/
menu:
  latest:
    identifier: auto-rebalancing
    parent: explore-planet-scale
    weight: 280
---

YugaByte DB automatically rebalances data into newly added nodes, so that the cluster can easily be expanded if more space is needed. In this tutorial, we will look at how YugaByte rebalances data while a workload is running. We will run a read-write workload using a pre-packaged sample application against a 3-node local universe with a replication factor of 3, and add nodes to it while the workload is running. We will then observe how the cluster rebalances its on-disk data as well as its memory footprint.

If you haven't installed YugaByte DB yet, do so first by following the [Quick Start](/quick-start/install/) guide.

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
      <i class="icon-docker"></i>
      Docker
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="macos" class="tab-pane fade show active" role="tabpanel" aria-labelledby="macos-tab">
    {{% includeMarkdown "binary/auto-rebalancing.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade" role="tabpanel" aria-labelledby="linux-tab">
    {{% includeMarkdown "binary/auto-rebalancing.md" /%}}
  </div>
  <!--
    <div id="docker" class="tab-pane fade" role="tabpanel" aria-labelledby="docker-tab">
    {{% includeMarkdown "docker/auto-rebalancing.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade" role="tabpanel" aria-labelledby="kubernetes-tab">
    {{% includeMarkdown "kubernetes/auto-rebalancing.md" /%}}
  </div>
  -->
</div>

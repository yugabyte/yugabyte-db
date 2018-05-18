---
title: 9. Global Distribution
linkTitle: 9. Global Distribution
description: Global Distribution
aliases:
  - /explore/global-distribution/
menu:
  latest:
    identifier: global-distribution
    parent: explore
    weight: 260
---

YugaByte DB can easily be deployed in a globally distributed manner to serve application queries from the region closest to the end users with low latencies as well as to survive any outages to ensure high availability.

This tutorial will simulate AWS regions on a local machine. First, we will deploy YugaByte DB in the `us-west-2` region across multiple availability zones (`a`, `b`, `c`). We will start a key-value workload against this universe. Next, we will change this setup to run across multiple geographic regions in US East (`us-east-1`) and Tokyo (`ap-northeast-1`), with the workload running during the entire transition.

If you haven't installed YugaByte DB yet, do so first by following the [Quick Start](../../quick-start/install/) guide.


<ul class="nav nav-tabs nav-tabs-yb">
  <li>
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
</ul>

<div class="tab-content">
  <div id="macos" class="tab-pane fade show active" role="tabpanel" aria-labelledby="macos-tab">
    {{% includeMarkdown "binary/global-distribution.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade" role="tabpanel" aria-labelledby="linux-tab">
    {{% includeMarkdown "binary/global-distribution.md" /%}}
  </div>
  <div id="docker" class="tab-pane fade" role="tabpanel" aria-labelledby="docker-tab">
    {{% includeMarkdown "docker/global-distribution.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade" role="tabpanel" aria-labelledby="kubernetes-tab">
    {{% includeMarkdown "kubernetes/global-distribution.md" /%}}
  </div>
</div>


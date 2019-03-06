---
title: Fault Tolerance
linkTitle: 2. Fault Tolerance
description: Fault Tolerance
aliases:
  - /explore/fault-tolerance/
  - /latest/explore/fault-tolerance/
  - /latest/explore/postgresql/fault-tolerance/
menu:
  latest:
    identifier: fault-tolerance
    parent: explore
    weight: 215
---

YugaByte DB can automatically handle failures and therefore provides [high availability](../../../architecture/core-functions/high-availability/) for YSQL tables. We will create these tables with a replication factor = 3 that allows a [fault tolerance](../../../architecture/concepts/docdb/replication/) of 1. This means the cluster will remain available for both reads and writes even if one node fails. However, if another node fails bringing the number of failures to 2, then writes will become unavailable on the cluster in order to preserve data consistency.


If you haven't installed YugaByte DB yet, do so first by following the [Quick Start](../../../quick-start/install/) guide.


<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#macos" class="nav-link active" id="macos-tab" data-toggle="tab" role="tab" aria-controls="macos" aria-selected="true">
      <i class="fab fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="#linux" class="nav-link" id="linux-tab" data-toggle="tab" role="tab" aria-controls="linux" aria-selected="false">
      <i class="fab fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>
  <li>
    <a href="#docker" class="nav-link" id="docker-tab" data-toggle="tab" role="tab" aria-controls="docker" aria-selected="false">
      <i class="fab fa-docker"></i>
      Docker
    </a>
  </li>
  <li>
    <a href="#kubernetes" class="nav-link" id="kubernetes-tab" data-toggle="tab" role="tab" aria-controls="kubernetes" aria-selected="false">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="macos" class="tab-pane fade show active" role="tabpanel" aria-labelledby="macos-tab">
    {{% includeMarkdown "binary/fault-tolerance.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade" role="tabpanel" aria-labelledby="linux-tab">
    {{% includeMarkdown "binary/fault-tolerance.md" /%}}
  </div>
  <div id="docker" class="tab-pane fade" role="tabpanel" aria-labelledby="docker-tab">
    {{% includeMarkdown "docker/fault-tolerance.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade" role="tabpanel" aria-labelledby="kubernetes-tab">
    {{% includeMarkdown "kubernetes/fault-tolerance.md" /%}}
  </div>
</div>

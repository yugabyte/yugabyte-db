---
title: Fault Tolerance
weight: 220
---

YugaByte DB can automatically handle failures and therefore provides [high availability](/architecture/core-functions/high-availability/) for both Redis as well as Cassandra tables. In this tutorial, we will look at how fault tolerance is achieved for Cassandra, but the same steps would work for Redis tables as well. Except for the Kubernetes example, we will create these tables with a replication factor = 5 that allows a [fault tolerance](/architecture/concepts/replication/) of 2. We will then insert some data through one of the nodes, and query the data from another node. We will then simulate two node failures (one by one) and make sure we are able to successfully query and write data after each of these failures.

If you haven't installed YugaByte DB yet, do so first by following the [Quick Start](/quick-start/install/) guide.

<ul class="nav nav-tabs nav-tabs-yb">
  <li class="active">
    <a href="#docker">
      <i class="icon-docker"></i>
      Docker
    </a>
  </li>
  <li>
    <a href="#kubernetes">
      <i class="fa fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
  <li >
    <a href="#macos">
      <i class="fa fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="#linux">
      <i class="fa fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="docker" class="tab-pane fade in active">
    {{% includeMarkdown "/explore/docker/fault-tolerance.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade">
    {{% includeMarkdown "/explore/kubernetes/fault-tolerance.md" /%}}
  </div>
  <div id="macos" class="tab-pane fade">
    {{% includeMarkdown "/explore/binary/fault-tolerance.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade">
    {{% includeMarkdown "/explore/binary/fault-tolerance.md" /%}}
  </div> 
</div>

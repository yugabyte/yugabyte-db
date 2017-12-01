---
title: Fault Tolerance
weight: 220
---

YugaByte DB can automatically handle failures and therefore provides [high availability](/architecture/core-functions/high-availability/) for both Redis as well as Cassandra tables. In this tutorial, we will look at how fault tolerance is achieved for CQL, but the same steps would work for Redis tables as well. We will create these tables with a replication factor = 5 that allows a [fault tolerance](/architecture/concepts/replication/) of 2. We will then insert some data through one of the nodes, and query the data from another node. We will then kill two nodes (one by one) and make sure we are able to successfully query and write data after each of these faults.

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
    {{% includeMarkdown "/explore/docker/fault-tolerance.md" /%}}
  </div>
  <div id="macos" class="tab-pane fade">
    {{% includeMarkdown "/explore/binary/fault-tolerance.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade">
    {{% includeMarkdown "/explore/binary/fault-tolerance.md" /%}}
  </div> 
</div>

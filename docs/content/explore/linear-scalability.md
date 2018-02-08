---
title: Linear Scalability
weight: 210
---

With YugaByte DB, you can add nodes to scale your cluster up very efficiently and reliably in order to achieve more read and write IOPS. In this tutorial, we will look at how YugaByte DB can scale while a workload is running. We will run a read-write workload using a pre-packaged sample application against a 3-node local cluster with a replication factor of 3, and add nodes to it while the workload is running. We will then observe how the cluster scales out, by verifying that the number of read/write IOPS are evenly distributed across all the nodes at all times.

If you haven't installed YugaByte DB yet, do so first by following the [Quick Start](/quick-start/install/) guide.

<ul class="nav nav-tabs">
  <li class="active">
    <a data-toggle="tab" href="#docker">
      <i class="icon-docker"></i>
      Docker
    </a>
  </li>
  <li>
    <a data-toggle="tab" href="#kubernetes">
      <i class="fa fa-cubes" aria-hidden="true"></i>
      Kubernetes
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
    {{% includeMarkdown "/explore/docker/linear-scalability.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade">
    {{% includeMarkdown "/explore/kubernetes/linear-scalability.md" /%}}
  </div>
  <div id="macos" class="tab-pane fade">
    {{% includeMarkdown "/explore/binary/linear-scalability.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade">
    {{% includeMarkdown "/explore/binary/linear-scalability.md" /%}}
  </div> 
</div>

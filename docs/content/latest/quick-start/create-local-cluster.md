---
title: 2. Create Local Cluster
linkTitle: 2. Create Local Cluster
description: Create Local Cluster
aliases:
  - /quick-start/create-local-cluster/
menu:
  latest:
    parent: quick-start
    weight: 120
type: page
---

After [installing YugaByte DB](../install/), follow the instructions below to create a local cluster.

<ul class="nav nav-tabs nav-tabs-yb">
  <li class="active">
    <a href="#docker">
      <i class="icon-docker"></i>
      Docker
    </a>
  </li>
  <li >
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
    {{% includeMarkdown "docker/create-local-cluster.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade">
    {{% includeMarkdown "kubernetes/create-local-cluster.md" /%}}
  </div>
  <div id="macos" class="tab-pane fade">
    {{% includeMarkdown "binary/create-local-cluster.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade">
    {{% includeMarkdown "binary/create-local-cluster.md" /%}}
  </div> 
</div>
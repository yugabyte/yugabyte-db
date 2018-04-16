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
  <li>
    <a href="#docker" class="nav-link active" id="docker-tab" data-toggle="tab" role="tab" aria-controls="docker" aria-selected="true">
      <i class="icon-docker"></i>
      Docker
    </a>
  </li>
  <li >
    <a href="#kubernetes" class="nav-link" id="kubernetes-tab" data-toggle="tab" role="tab" aria-controls="kubernetes" aria-selected="false">
      <i class="fa fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
  <li >
    <a href="#macos" class="nav-link" id="macos-tab" data-toggle="tab" role="tab" aria-controls="macos" aria-selected="false">
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
  <div id="docker" class="tab-pane fade show active" role="tabpanel" aria-labelledby="docker-tab">
    {{% includeMarkdown "docker/create-local-cluster.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade" role="tabpanel" aria-labelledby="kubernetes-tab">
    {{% includeMarkdown "kubernetes/create-local-cluster.md" /%}}
  </div>
  <div id="macos" class="tab-pane fade" role="tabpanel" aria-labelledby="macos-tab">
    {{% includeMarkdown "binary/create-local-cluster.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade" role="tabpanel" aria-labelledby="linux-tab">
    {{% includeMarkdown "binary/create-local-cluster.md" /%}}
  </div> 
</div>
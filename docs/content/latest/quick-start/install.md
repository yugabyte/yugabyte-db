---
title: 1. Install YugaByte DB
linkTitle: 1. Install YugaByte DB
description: Install YugaByte DB
aliases:
  - /quick-start/install/
menu:
  latest:
    parent: quick-start
    weight: 110
type: page
---

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
    {{% includeMarkdown "docker/install.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade">
    {{% includeMarkdown "kubernetes/install.md" /%}}
  </div>
  <div id="macos" class="tab-pane fade">
    {{% includeMarkdown "binary/macos-install.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade">
    {{% includeMarkdown "binary/linux-install.md" /%}}
  </div> 
</div>



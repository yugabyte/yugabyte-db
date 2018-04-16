---
title: Local SSD
linkTitle: Local SSD
description: Local SSD
menu:
  latest:
    identifier: local-ssd
    parent: deploy-kubernetes
    weight: 611
---

This tutorial will cover how to deploy YugaByte DB on Kubernetes StatefulSets using locally mounted SSDs as the data disks.

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#gke">
      <i class="icon-shell"></i>
      GKE
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="gke" class="tab-pane fade in active">
    {{% includeMarkdown "local-ssd/gke.md" /%}}
  </div>
</div>
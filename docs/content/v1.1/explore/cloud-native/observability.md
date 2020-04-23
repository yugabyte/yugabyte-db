---
title: Observability with Prometheus
linkTitle: 3. Observability 
description: Observability with Prometheus
block_indexing: true
menu:
  v1.1:
    identifier: observability
    parent: explore-cloud-native
    weight: 216
---

You can monitor your local YugabyteDB cluster with a local instance of [Prometheus](https://prometheus.io/), the de-facto standard for time-series monitoring of cloud native infrastructure. Every YugabyteDB service exposes metrics in the Prometheus format at the `/prometheus-metrics` endpoint.

If you haven't installed YugabyteDB yet, do so first by following the [Quick Start](../../../quick-start/install/) guide.

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#macos" class="nav-link active" id="macos-tab" data-toggle="tab" role="tab" aria-controls="macos" aria-selected="true">
      <i class="fab fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="#linux" class="nav-link" id="linux-tab" data-toggle="tab" role="tab" aria-controls="linux" aria-selected="v">
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
  <!--
  <li>
    <a href="#kubernetes" class="nav-link" id="kubernetes-tab" data-toggle="tab" role="tab" aria-controls="kubernetes" aria-selected="false">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
-->
</ul>

<div class="tab-content">
  <div id="macos" class="tab-pane fade show active" role="tabpanel" aria-labelledby="macos-tab">
    {{% includeMarkdown "binary/observability.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade" role="tabpanel" aria-labelledby="linux-tab">
    {{% includeMarkdown "binary/observability.md" /%}}
  </div>
  <div id="docker" class="tab-pane fade" role="tabpanel" aria-labelledby="docker-tab">
    {{% includeMarkdown "docker/observability.md" /%}}
  </div>

</div>


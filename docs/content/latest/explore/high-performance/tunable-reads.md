---
title: Tunable Reads
linkTitle: Tunable Reads
description: Tunable Read Latency
aliases:
  - /explore/tunable-reads/
  - /latest/explore/tunable-reads/
menu:
  latest:
    identifier: tunable-reads
    parent: explore-high-perf
    weight: 245
---

With YugaByte DB, you can choose different consistency levels for performing reads. Relaxed consistency levels lead to lower latencies since the DB now has less work to do at read time including serving the read from the tablet followers. Reading from the followers is similar to reading from a cache, which can give more read IOPS with low latency. In this tutorial, we will update a single key-value over and over, and read it from the tablet leader. While that workload is running, we will start another workload to read from a follower and verify that we are able to read from a tablet follower.

## Bounded Staleness
YugaByte DB also allows you to specify the maximum staleness of data when reading from tablet followers. This means that if the follower hasn't heard from the leader for the specified amount of time, the read request will be forwarded to the leader. This is particularly useful when the tablet follower is located far away from the tablet leader. To enable this feature, you will need to create your cluster with the custom tserver flag `max_stale_read_bound_time_ms`. See [Creating a local cluster with custom flags](../../../admin/yb-ctl/#creating-a-local-cluster-with-custom-flags) for instructions on how to do this.

If you haven't installed YugaByte DB yet, do so first by following the [Quick Start](../../../quick-start/install/) guide.

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
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
</ul>

<div class="tab-content">
  <div id="macos" class="tab-pane fade show active" role="tabpanel" aria-labelledby="macos-tab">
    {{% includeMarkdown "binary/tunable-reads.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade" role="tabpanel" aria-labelledby="linux-tab">
    {{% includeMarkdown "binary/tunable-reads.md" /%}}
  </div>
  <!--
  <div id="docker" class="tab-pane fade" role="tabpanel" aria-labelledby="docker-tab">
    {{% includeMarkdown "docker/tunable-reads.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade" role="tabpanel" aria-labelledby="kubernetes-tab">
    {{% includeMarkdown "kubernetes/tunable-reads.md" /%}}
  </div>
  -->
</div>

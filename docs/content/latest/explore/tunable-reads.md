---
title: 6. Tunable Reads
linkTitle: 6. Tunable Reads
description: Tunable Read Latency
aliases:
  - /explore/tunable-reads/
menu:
  latest:
    identifier: tunable-reads
    parent: explore
    weight: 260
---

With YugaByte DB, you can choose different consistency levels for performing reads. Relaxed consistency levels lead to lower latencies since the DB now has less work to do at read time including serving the read from the tablet followers. Reading from the followers is similar to a reading from a cache, which can give more read IOPS with low latency. In this tutorial, we will update a single key-value over and over, and read it from the tablet leader. While that workload is running, we will start another workload to read from a follower and verify that we are able to read from a tablet follower.

If you haven't installed YugaByte DB yet, do so first by following the [Quick Start](../../quick-start/install/) guide.

<ul class="nav nav-tabs nav-tabs-yb">
  <li class="active">
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
  <div id="docker" class="tab-pane fade">
    {{% includeMarkdown "docker/tunable-reads.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade">
    {{% includeMarkdown "kubernetes/tunable-reads.md" /%}}
  </div>
  <div id="macos" class="tab-pane fade in active">
    {{% includeMarkdown "binary/tunable-reads.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade">
    {{% includeMarkdown "binary/tunable-reads.md" /%}}
  </div> 
</div>

---
title: Tunable Reads
weight: 260
---

With YugaByte DB, you can choose different consistency levels for performing reads. This includes reading from the tablet leader or the followers. Reading from the followers is similar to a reading from a cache, which can give more read IOPS with low latency. In this tutorial, we will update a single key-value over and over, and read it from the tablet leader. While that workload is running, we will start another workload to read from a follower and verify that we are able to read from a tablet follower.

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
    {{% includeMarkdown "/explore/docker/tunable-reads.md" /%}}
  </div>
  <div id="macos" class="tab-pane fade">
    {{% includeMarkdown "/explore/binary/tunable-reads.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade">
    {{% includeMarkdown "/explore/binary/tunable-reads.md" /%}}
  </div> 
</div>

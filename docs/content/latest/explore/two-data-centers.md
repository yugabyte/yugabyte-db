---
title: Two data center deployment
linkTitle: Two data center deployment
description: Two data center (2DC) deployment
menu:
  latest:
    identifier: two-data-centers
    parent: explore
    weight: 250
---

By default, YugabyteDB universes provide synchronous replication and strong consistency across geo-distributed data centers. But sometimes asynchronous replication will meet your need for disaster recovery, auditing and compliance, and other applications. For more information, see [Two data center (2DC) deployments](../../architecture/2dc-deployments/) in the Architecture section.

This tutorial simulates a geo-distributed two data center (2DC) deployment using two local YugabyteDB clusters, one representing "Data Center - East" and the other representing "Data Center - West." Explore unidirectional (one-way) asynchronous replication and bidirectional (two-way) asynchronous replication using the `yb-ctl` and `yb-admin` utilities on your laptop or another local machine.

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
    {{% includeMarkdown "binary/two-data-centers.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade" role="tabpanel" aria-labelledby="linux-tab">
    {{% includeMarkdown "binary/two-data-centers.md" /%}}
  </div>
</div>


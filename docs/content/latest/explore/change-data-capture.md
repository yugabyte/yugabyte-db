---
title: Change data capture (CDC)
linkTitle: Change data capture (CDC)
description: Change data capture (CDC) 
menu:
  latest:
    identifier: change-data-capture
    parent: explore
    weight: 249
aliases:
- /latest/deploy/cdc/cdc-to-stdout
---

[Change data capture (CDC)](../../architecture/cdc-architecture) can be used to asynchronously stream data changes from a YugabyteDB cluster to external systems like message queues and OLAP warehouses. The data changes in YugabyteDB are detected, captured, and then output to the specified target.  In the steps below, you will use a local YugabyteDB cluster to stream data changes to `stdout` using the CDC API. 

If you haven't installed YugabyteDB yet, do so first by following the [Quick Start](../../quick-start/install/) guide.


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
    {{% includeMarkdown "binary/change-data-capture.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade" role="tabpanel" aria-labelledby="linux-tab">
    {{% includeMarkdown "binary/change-data-capture.md" /%}}
  </div>
</div>


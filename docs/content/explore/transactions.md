---
title: Distributed ACID Transactions
weight: 230
---

Distributed ACID transactions batch a multi-step, multi-table operation into a single, all-or-nothing operation. The intermediate states of the database between the steps in a transaction are not visible to other concurrent transactions or the end user. If the transaction encounters any failures that prevents it from completing successfully, none of the steps are applied to the database.

YugaByte DB is designed to support transactions at the following isolation levels:

- Serializable (currently supported)
- Snapshot Isolation (work in progress)

You can [read more about transactions](/architecture/transactions/) in our architecture docs.

If you haven't installed YugaByte DB yet, do so first by following the [Quick Start](/quick-start/install/) guide.

<ul class="nav nav-tabs">
  <li class="active">
    <a data-toggle="tab" href="#docker">
      <i class="icon-docker"></i>
      Docker
    </a>
  </li>
  <li>
    <a data-toggle="tab" href="#kubernetes">
      <i class="fa fa-cubes" aria-hidden="true"></i>
      Kubernetes
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
    {{% includeMarkdown "/explore/docker/transactions.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade">
    {{% includeMarkdown "/explore/kubernetes/transactions.md" /%}}
  </div>
  <div id="macos" class="tab-pane fade">
    {{% includeMarkdown "/explore/binary/transactions.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade">
    {{% includeMarkdown "/explore/binary/transactions.md" /%}}
  </div> 
</div>

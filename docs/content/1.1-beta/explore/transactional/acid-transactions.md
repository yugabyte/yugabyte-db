---
title: 1. ACID Transactions
linkTitle: 1. ACID Transactions
description: Distributed ACID Transactions
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
aliases:
  - /explore/transactions/
  - /latest/explore/transactions/
menu:
  1.1-beta:
    identifier: transactions
    parent: explore-transactional
    weight: 225
---

Distributed ACID transactions batch a multi-step, multi-table operation into a single, all-or-nothing operation. The intermediate states of the database between the steps in a transaction are not visible to other concurrent transactions or the end user. If the transaction encounters any failures that prevents it from completing successfully, none of the steps are applied to the database.

YugaByte DB is designed to support transactions at the following isolation levels:

- Snapshot Isolation (currently supported)
- Serializable (work in progress)

You can [read more about transactions](/architecture/transactions/) in our architecture docs.

If you haven't installed YugaByte DB yet, do so first by following the [Quick Start](/quick-start/install/) guide.

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#macos" class="nav-link active" id="macos-tab" data-toggle="tab" role="tab" aria-controls="macos" aria-selected="true">
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
  <li>
    <a href="#docker" class="nav-link" id="docker-tab" data-toggle="tab" role="tab" aria-controls="docker" aria-selected="false">
      <i class="icon-docker"></i>
      Docker
    </a>
  </li>
  <li>
    <a href="#kubernetes" class="nav-link" id="kubernetes-tab" data-toggle="tab" role="tab" aria-controls="kubernetes" aria-selected="false">
      <i class="fa fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="macos" class="tab-pane fade show active" role="tabpanel" aria-labelledby="macos-tab">
    {{% includeMarkdown "binary/transactions.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade" role="tabpanel" aria-labelledby="linux-tab">
    {{% includeMarkdown "binary/transactions.md" /%}}
  </div>
  <div id="docker" class="tab-pane fade" role="tabpanel" aria-labelledby="docker-tab">
    {{% includeMarkdown "docker/transactions.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade" role="tabpanel" aria-labelledby="kubernetes-tab">
    {{% includeMarkdown "kubernetes/transactions.md" /%}}
  </div>
</div>

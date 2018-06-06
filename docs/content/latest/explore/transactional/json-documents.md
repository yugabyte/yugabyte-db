---
title: 3. JSON Documents
linkTitle: 3. JSON Documents
description: JSON Documents
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
aliases:
  - /explore/json-documents/
  - /latest/explore/json-documents/
menu:
  latest:
    identifier: json-documents
    parent: explore-transactional
    weight: 235
---

The JSON document data type makes it very easy to model use-cases which require a flexible schema. YugaByte DB supports a `JSONB` data type that parse, store and query JSON documents natively.

In this tutorial, we are going to look the example of an online bookstore. The bookstore has a number of books, each of these books has a number of attributes describing it like the name, the author, and so on. We are going to model these book details as a JSON object using the JSONB datatype.

If you haven't installed YugaByte DB yet, do so first by following the [Quick Start](../../quick-start/install/) guide.

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
    {{% includeMarkdown "binary/json-documents.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade" role="tabpanel" aria-labelledby="linux-tab">
    {{% includeMarkdown "binary/json-documents.md" /%}}
  </div>
  <div id="docker" class="tab-pane fade" role="tabpanel" aria-labelledby="docker-tab">
    {{% includeMarkdown "docker/json-documents.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade" role="tabpanel" aria-labelledby="kubernetes-tab">
    {{% includeMarkdown "kubernetes/json-documents.md" /%}}
  </div>
</div>

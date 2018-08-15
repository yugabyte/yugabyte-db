---
title: 5. Batch Operations
linkTitle: 5. Batch Operations
description: Batch Operations
aliases:
  - /develop/learn/batch-operations/
menu:
  1.1-beta:
    identifier: batch-operations
    parent: learn
    weight: 568
---

Batch operations is the ability to send a set of operations as one operation (RPC call) to the database instead of sending the operations one by one as individual RPC calls. For use-cases requiring high throughput, batch operations can be very effcient since it is possible to reduce the overhead of multiple RPC calls. The larger the batch size, the higher the latency for the entire batch. Although the latency for the entire batch of operations is higher than the latency of any single operation, the throughput of the batch of operations is much higher.

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#cassandra" class="nav-link active" id="cassandra-tab" data-toggle="tab" role="tab" aria-controls="cassandra" aria-selected="true">
      <i class="icon-cassandra" aria-hidden="true"></i>
      Cassandra
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="cassandra" class="tab-pane fade show active" role="tabpanel" aria-labelledby="cassandra-tab">
    {{% includeMarkdown "cassandra/batch-operations.md" /%}}
  </div>
</div>

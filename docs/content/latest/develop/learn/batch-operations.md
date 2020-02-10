---
title: 6. Batch operations
linkTitle: 6. Batch operations
description: Batch operations
menu:
  latest:
    identifier: batch-operations
    parent: learn
    weight: 568
---

Batch operations is the ability to send a set of operations as one operation (RPC call) to the database instead of sending the operations one by one as individual RPC calls. For use cases requiring high throughput, batch operations can be very efficient since it is possible to reduce the overhead of multiple RPC calls. The larger the batch size, the higher the latency for the entire batch. Although the latency for the entire batch of operations is higher than the latency of any single operation, the throughput of the batch of operations is much higher.

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/latest/develop/learn/batch-operations" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="/latest/develop/learn/batch-operations-ycql" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

Coming soon.

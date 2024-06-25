---
title: Batch operations in YSQL
headerTitle: Batch operations
linkTitle: Batch operations
description: Learn how batch operations in YSQL send a set of operations as a single RPC call rather than one by one as individual RPC calls.
menu:
  v2.20:
    identifier: batch-operations-2-ysql
    parent: learn
    weight: 568
type: docs
private: true
---
<!-- Page DISABLED for lack of content -->
{{<api-tabs>}}

Batch operations is the ability to send a set of operations as one operation (RPC call) to the database instead of sending the operations one by one as individual RPC calls. For use cases requiring high throughput, batch operations can be very efficient since it is possible to reduce the overhead of multiple RPC calls. The larger the batch size, the higher the latency for the entire batch. Although the latency for the entire batch of operations is higher than the latency of any single operation, the throughput of the batch of operations is much higher.

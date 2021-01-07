---
title: Optimize queries with EXPLAIN
linkTitle: Explain Analyze
description: Query optimization with EXPLAIN and EXPLAIN ANALYZE
menu:
  latest:
    identifier: 
    parent: 
    weight: 
isTocNested: true
showAsideToc: true
---
## Test
For each query that YSQL receives, it creates an execution plan which you can access using the *EXPLAIN* statement. The plan not only estimates the initial cost before the first row is returned, but also provides an estimate of the total execution cost for the whole result set.


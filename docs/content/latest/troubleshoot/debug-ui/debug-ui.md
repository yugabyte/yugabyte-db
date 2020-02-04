---
title: Debug UIs
linkTitle: Debug UIs
description: Debug UIs
aliases:
  - /troubleshoot/debug-ui/
menu:
  latest:
    identifier: troubleshoot-debug-ui
    parent: troubleshoot
    weight: 707
isTocNested: true
showAsideToc: true
---

WIP

## How to get to these from YW
Proxy pages from per-universe `Nodes` tab.

## Master specific pages
### Home view
Master quorum
Cluster config
Master redirect
Load balancer status

### Tables view
User tables
Index tables
System tables
Yedis

Clicking on a table -- per table view

### Tablet servers view
Heartbeat times
Bootstrapping data
Per az distribution

## Tserver specific pages
### Home page
Transactions -- this is unused atm?
Maintenance Manager -- do we even use this?

### Tables page
Name, UUID, state, sizes, raft roles

### Tablets page
Name, UUIDs, partition, state, sizes, raft config

#### Per-tablet view
Schema
Raft consensus state
Raft WAL anchors

### Utilities

## Generic debug pages

### Footer across pages

### Utilities
Logs
Gflags
Memory tracker heirarchy
TCMalloc stats
Metrics (json vs prometheus)
Threads view
Live RPCs -- TSERVER vs MASTER views

Async tasks -- MASTER ONLY

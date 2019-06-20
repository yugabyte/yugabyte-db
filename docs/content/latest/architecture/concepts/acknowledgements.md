---
title: Acknowledgements
linkTitle: Acknowledgements
description: Acknowledgements
aliases:
  - /architecture/concepts/acknowledgements/
menu:
  latest:
    identifier: architecture-acknowledgements
    parent: architecture-concepts
    weight: 1130
---

The YugaByte DB codebase has leveraged several open source projects as a starting point.

* Postgres stateless language layer for implementing YSQL.

* Postgres' scanner/parser modules (.y/.l files) were used as a starting point for implementating YCQL scanner/parser in C++.

* DocDB's document storage layer uses a highly customized/enhanced version of RocksDB. A sample of the customizations and enhancements we have done are described [in this section](../../docdb/persistence/).

* We used Apache Kudu's Raft implementation & the server framework as a starting point. Since then, we have implemented several enhancements such as leader leases & pre-voting state during learner mode for correctness, improvements to the network stack, auto balancing of tablets on failures, zone/DC aware data placement, leader-balancing, ability to do full cluster moves in a online manner, and more.

* Google Libraries (glog, gflags, protocol buffers, snappy, gperftools, gtest, gmock).
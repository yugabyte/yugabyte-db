---
title: Acknowledgements
weight: 980
---

The YugaByte DB code base has leveraged several open source projects as a starting point.

* Google Libraries (glog, gflags, protocol buffers, snappy, gperftools, gtest, gmock).

* The DocDB layer uses a highly customized/enhanced version of RocksDB. A sample of the customizations and enhancements we have done are described [in this section](/architecture/concepts/persistence/#introduction).

* We used Apache Kudu's Raft implementation & the server framework as a starting point. Since then, we have implemented several enhancements such as leader-leases & pre-voting state during learner mode for correctness, improvements to the network stack, auto balancing of tablets on failures, zone/DC aware data placement, leader-balancing, ability to do full cluster moves in a online manner, and so on to name just a few.

* Postgres' scanner/parser modules (.y/.l files) were used as a starting point for implementating CQL (Apache Cassandra Query Language) scanner/parser in C++.

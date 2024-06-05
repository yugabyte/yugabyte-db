---
title: Third-party software
headerTitle: Third-party software
linkTitle: Third-party software
description: Learn about the third-party software contained in YugabyteDB.
menu:
  preview:
    parent: legal
    identifier: third-party-software
    weight: 30
type: docs
---

## Acknowledgements

Yugabyte proudly participates in the open source community and appreciates all open source contributions that have been incorporated into the YugabyteDB open source project. The YugabyteDB codebase has leveraged several open source projects as a starting point, including the following:

- PostgreSQL stateless language layer for implementing YSQL.
- PostgreSQL scanner and parser modules (`.y` and `.l` files) were used as a starting point for implementing the YCQL scanner and parser in C++.
- The DocDB document store uses a customized and enhanced version of [RocksDB](https://github.com/facebook/rocksdb). Some of the customizations and enhancements are described in [DocDB store](/preview/architecture/docdb/).
- The Apache Kudu Raft implementation and server framework were used as a starting point. Since then, Yugabyte has implemented several enhancements, such as leader leases and pre-voting state during learner mode for correctness, improvements to the network stack, auto balancing of tablets on failures, zone/DC aware data placement, leader-balancing, ability to do full cluster moves in a online manner, and more.
- Google libraries (`glog`, `gflags`, `protocol buffers`, `snappy`, `gperftools`, `gtest`, `gmock`).

## Third-party software components

Yugabyte products incorporate third party software, which includes the copyrighted, patented, or otherwise legally protected software of third parties.

The release notes for every [stable (even-numbered) release](/preview/releases/) include a list of the third-party open-source software components that are incorporated into YugabyteDB products, along with license information for each of those third-party components.

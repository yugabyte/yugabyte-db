# Codemap

The YugabyteDB code is split into two top-level sections:

* [`postgres`](src/postgres/) is our modified fork of the Postgresql code. This is mostly C code.

* [`yb`](src/yb/) is the core of the YugabyteDB storage engine. This is mostly C++ code.

**The core storage engine is split into the following C++ components**:

* [`bfpg`](src/yb/bfpg/) TODO

* [`bfql`](src/yb/bfql/) TODO

* [`cdc`](src/yb/cdc/) is the main code for the Xcluster Replication feature and the CDCSDK, generalized Change Data Capture feature.

* [`client`](src/yb/client/) is the underlying client component used for RPC communication between servers.

* [`common`](src/yb/common/) is code shared across server, client and query components.

* [`consensus`](src/yb/consensus/) is the core Raft consensus implementation.

* [`docdb`](src/yb/docdb/) is the DocDB encoding implementation

* [`encryption`](src/yb/encryption/) is a set of utilities for encryption related work, such as TLS and Encryption at Rest.

* [`fs`](src/yb/fs/) covers the abstractions for manipulating the underlying file systems.

* [`gen_yrpc`](src/yb/gen_yrpc/) covers the abstraction on top of our protobuf usage, for generating server side code.

* [`gutil`](src/yb/gutil/) is for utilities to augment the standard library, from the upstream Chromium project.

* [`integration-tests`](src/yb/integration-tests/) is strictly used for tests which depend on several components in the code.

* [`master`](src/yb/master/) is the control path server side of the database. This is responsible for DDLs, Cluster balancing, health checking, etc.

* [`rocksdb`](src/yb/rocksdb/) is our heavily modified fork of the RocksDB single-node storage library.

* [`rocksutil`](src/yb/rocksutil/) covers utilities for working with RocksDB.

* [`rpc`](src/yb/rpc/) is the underlying RPC layer implementation.

* [`server`](src/yb/server/) cover abstract classes used by both master and tserver processes.

* [`tablet`](src/yb/tablet/) is the main data path IO logic, both for single shard and multi shard transactions.

* [`tools`](src/yb/tools/) contains command line utilities to help debug, inspect or modify state on a live running cluster.

* [`tserver`](src/yb/tserver/) is the data path server side of the database. Responsible internally for managing tablets and externally for communication with master and the Postgres clients.

* [`util`](src/yb/util/) covers utilities used across the entire code base. These range from low level atomic abstractions or memory management primitives, to higher level thread pools, metrics and gflag handling.

* [`yql`](src/yb/yql/) covers query layer abstractions. This contains both server side code for YCQL and YEDIS, as well as the C to C++ transition for our YSQL layer.

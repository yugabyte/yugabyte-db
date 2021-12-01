This directory contains design documents with details of how various features work internally. The intended audience for these documents are commiters to the codebase and users wanting a deep understanding of what happens under the hood for various features.

You can find the user-facing [YugabyteDB docs here](https://docs.yugabyte.com/). If you want to understand the architecture of YugabyteDB, the [architecture section in the docs](https://docs.yugabyte.com/latest/architecture/) is a good place to start.


Below you can find a table of high level and non-trivial features, relevant design documents for them, as well as the main reference engineers.

| Feature | Main reference |
| -------- | ------------ |
|[Distributed backup / restore](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/distributed-backup-and-restore.md)|Sergei, Oleg|
|[Distributed PITR](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/distributed-backup-point-in-time-recovery.md)|Sergei|
|[Automatic tablet splitting](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/docdb-automatic-tablet-splitting.md)|Timur|
|[Async xCluster replication](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/multi-region-xcluster-async-replication.md)|Rahul, Nicolas|
|[Online index backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md)|Amitanand|
|[Encryption at rest](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/docdb-encryption-at-rest.md)|Rahul|
|Cluster load balancing|Rahul, Sanket|
|Master DDL operation handling|TBD|
|YCQL virtual system tables|TBD|
|Tablet server heartbeat|Nicolas|
|Tablet local bootstrap|TBD|
|Tablet remote bootstrap|TBD|
|Raft consensus|TBD|
|Raft read replica support|Rahul|
|Read from followers|TBD|
|DocDB encoding|TBD|
|Distributed transactions|Sergei|
|RocksDB|Timur, Sergei|
|[RocksDB: Advanced delta-encoding](advanced-delta-encoding.md)|Timur|
|TLS|Sergei|
|Yugabyted|Sanket|
|Command line tools|TBD|
|Admin debug UIs|TBD|
|YEDIS server component|Amitanand|
|YCQL server component|TBD|
|[YSQL colocated tables](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/ysql-colocated-tables.md)|Jason|
|[YSQL tablespaces](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/wip-ysql-tablespaces.md)|Deepthi|
|[YSQL tablegroups](ysql-tablegroups.md)|Mihnea|
|[YSQL row level partitioning](ysql-row-level-partitioning.md)|Deepthi|

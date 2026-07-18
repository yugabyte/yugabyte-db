This directory contains design documents with details of how various features work internally. The intended audience for these documents are commiters to the codebase and users wanting a deep understanding of what happens under the hood for various features.

You can find the user-facing [YugabyteDB docs here](https://docs.yugabyte.com/). If you want to understand the architecture of YugabyteDB, the [architecture section in the docs](https://docs.yugabyte.com/latest/architecture/) is a good place to start.


Below you can find a table of high level and non-trivial features, relevant design documents for them, as well as the main reference engineers.

| Feature | Main reference |
| -------- | ------------ |
|Admin debug UIs|TBD|
|[Async xCluster replication](multi-region-xcluster-async-replication.md)|Julien, Rahul, Nicolas|
|[Auto Flags](auto_flags.md)|Hari|
|[Automatic tablet splitting](docdb-automatic-tablet-splitting.md)|Timur|
|Cluster load balancing|Julien, Rahul, Sanket|
|Command line tools|TBD|
|[Distributed backup / restore](distributed-backup-and-restore.md)|Oleg, Sanket, Sergei|
|[Distributed PITR](distributed-backup-point-in-time-recovery.md)|Sanket, Sergei|
|Distributed transactions|Mikhail B, Sergei|
|DocDB encoding|TBD|
|[Encryption at rest](docdb-encryption-at-rest.md)|Rahul|
|Master DDL operation handling|TBD|
|[Online index backfill](online-index-backfill.md)|Amitanand|
|Raft consensus|TBD|
|Raft read replica support|Rahul|
|Read from followers|Amit|
|RocksDB|Timur, Sergei|
|[RocksDB: Advanced delta-encoding](advanced-delta-encoding.md)|Timur|
|Tablet local bootstrap|TBD|
|Tablet remote bootstrap|Amit|
|Tablet server heartbeat|Nicolas|
|TLS|Sergei|
|YCQL server component|TBD|
|YCQL virtual system tables|TBD|
|YEDIS server component|Amitanand|
|[YSQL colocated tables](ysql-colocated-tables.md)|Jason|
|[YSQL row level partitioning](ysql-row-level-partitioning.md)|Deepthi|
|[YSQL tablegroups](ysql-tablegroups.md)|Mihnea|
|[YSQL tablespaces](wip-ysql-tablespaces.md)|Deepthi|
|Yugabyted|Sanket|

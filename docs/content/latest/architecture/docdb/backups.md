---
title: Snapshots, backups and PITR
headerTitle: Snapshots, backups and PITR
linkTitle: Snapshots, backups and PITR
description: Learn how DocDB manages backup restore operations using rocksdb level snapshots.
aliases:
  - /latest/architecture/concepts/docdb/backups/
menu:
  latest:
    identifier: docdb-backups
    parent: docdb
    weight: 1149
isTocNested: true
showAsideToc: true
---

## Distributed backup and restore

The traditional way to take backups in an RDBMS is to dump the data across all (or the desired set of) the tables in the database by performing a scan operation. However, in YugabyteDB, a distributed SQL database that is often considered for its massive scalability, the data set could become quite large, making a scan-based backup practically infeasible. The distributed backups and restore feature is aimed at making backups and restores very efficient even on large data sets.

Note: An in-cluster snapshot refers to a read-only copy of the database created by creating a list of immutable files. A backup generally is an off-cluster copy of a snapshot of the database.

More details available in the [design doc](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/distributed-backup-and-restore.md).

### Goals

TBD from design doc

### Single tablet snapshots

- Native RocksDB operation.
- Flushing RocksDB memstore on all peers.
- Not needing Raft WAL entries anymore, can strictly rely on RocksDB files.
- Snapshot lives on every raft peer, for fault tolerance.

### Picking a timestamp

TBD

### Snapshot on all involved tablets

TBD from design doc
- reference to HT filter
- reference to history retention

### Glue together into a backup

TBD
- distributed snapshot
- schema from master

## Point-in-time recovery (PITR)

TBD
- Why schedules? Needs to happen automatically
- Scope? keyspace / database
- Retention period / interval? History retention reference

### Usage

TBD example usage?

### Data rollback

TBD why HT filter + big enough retention + distributed snapshots are good enough.

### Metadata rollback

TBD
- Metadata storage on master in a single table, raft replicated.
- Snapshot of master tablet.
- Venn diagram of before and after data.

#### Undo of CREATE TABLE

TBD

#### Undo of DROP TABLE

TBD

#### Undo of ALTER TABLE

TBD

#### Undo of TRUNCATE TABLE

TBD


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

Our tablet level snapshot implementation is based on the existing snapshot mechanism available in RocksDB. This is implemented as a highly efficient operation, where upon processing the snapshot operation, we will:
- Flush the data in the RocksDB memstore to disk, generating a new SST file. This is done to ensure we dump to disk all of the writes in the system, at the time of the request.
- Generate hardlinks of all the RocksDB SST files into a separate `.snapshots` directory, corresponding to this snapshot UUID. Since this operation uses hardlinks, it is generally very fast.
- Since RocksDB is an LSM database, the SST files are immutable and would only be deleted, if compacted away. This will happen naturally for the active RocksDB directory for the tablet. However, since every snapshot holds hardlinks to its respective set of files, even if the original files get deleted, the snapshot retains its data.

Furthermore, our snapshots are implemented as an actual Raft level operation, that is quorum replicated to all peers of a tablet. This allows us to get several benefits to the process:
- Since we have modified RocksDB to not use its WAL (TBD LINK), but instead rely on the Raft WAL instead, for durability, we would need to keep track of the Raft WAL entries as well, for a complete picture of a tablet's data. However, by forcing a flush of RocksDB when processing the snapshot operation, we can guarantee that the data in the snapshot is a complete view of the system, at the time of the request, across all peers. This allows us to not keep track of any Raft WAL entries with the snapshot data and instead rely strictly on the RocksDB files.
- Since the snapshots have the same lifecycle as RocksDB, which is bound to the lifecycle of a Raft tablet peer, we then get the same fault tolerance properties of raft, for all the snapshots. This means, as tablets move around in the cluster, so do their corresponding snapshots. Moreover, the replication factor of normal data also applies to snapshot data.

### Picking a timestamp

TBD from design doc

### Snapshot on all involved tablets

TBD from design doc

- reference to HT filter
- reference to history retention

### Glue together into a backup

TBD from design doc

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


# Point In Time Recovery and Incremental Backups

## Goals
Point in time recovery (also referred to as *PITR* in this document) and incremental backups go hand in hand. These two features help in recovering from a number of error or failure scenarios by allowing the database to be rolled back to a specific point in time (in the past). The rollback is done from the last full backup, with the updates since that full backup being replayed till the desired point in time.

Point in time restores and incremental backups depend on full backups (also referred to as *base backups*). A full backup, as the name suggests, is a complete transactional backup of data up to a certain point in time. The entire data set in the database is backed up for the set of namespaces/tables chosen by the user. Read more about the [design for a full, distributed backup](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/distributed-backup-and-restore.md). Full backups are deemed expensive for the following reasons:
* The performance of the database could be adversely affected. There may be a high latency of foreground operations, decreased throughput when the backup is happening.
* The data set size may be large, requiring more time, bandwidth and disk space to perform the backup.

The key design goals for this feature are listed below.

#### 1. RPO needs to be low
The primary metric for this scenario is the recovery point objective or RPO. This is the data lost when a backup is restored, expressed as time. For example, an RPO of 1 hour would require losing no more than the last 1 hour of data.

#### 2. Impact on cluster / cost should be low
While it is important to ensure a low RPO, it often becomes essential to trade it off with a reasonable cost / impact of achieving it. The cost / impact of backups could get high because of the following reasons:
* High CPU usage while taking a backup
* Higher latencies / lower throughput while taking backups
* The size of the backup is large, which would increase the network bandwidth utilization
* Larger backups are also more expensive to store

#### 3. RTO needs to be reasonable
Note that while recovery time objective or RTO needs to be reasonable, it is typically not an important metric in the case of a backup restore. This is especially true in the case of YugabyteDB, where regular outages have a very low RTO since the database is inherently highly available.

## Recovery Scenarios

This feature will address the following recovery scenarios.

#### A. Recover from app and operator errors
Point in time recovery should allow recovering from the following scenarios by rolling the database to a point in time before the error occurred. The errors could be one of the following:
1. DDL errors: A table is dropped by mistake
2. DML errors: An erroneous UPDATE statement is run on the table

In both the above cases, the table is rolled back to a point in time before the error occurred. 

#### B. Recover from disk or filesystem corruption
Data loss can happen due to one of the following reasons:
1. Loss of a disk
2. Erroneous deletion of DB data files: for example due to an operator error
3. Introduction of bugs in the database software: for example, due to a software upgrade

In a distributed SQL database such as YugabyteDB, scenarios #1 and #2 can be mitigated due to the presence of live replicas since it is highly unlikely the same issue occurs on all nodes. However, for scenario #3, point in time recovery is an important solution.

#### C. Recover from disaster scenarios
This is the scenario when the data in the entire source cluster is lost irrecoverably, and a restore needs to be performed from a remote location. While the likelihood of this scenario is low, it is still important to understand the probability of correlated failures. For example, a disaster due to a natural disaster has a very low probability of occurrence in a multi-region deployment, and it’s probability of occurrence increases with the proximity of the replicas.

## Terminology

The sections below describe the various features that will enable PITR and incremental backups, with a view to standardizing the terminology that will be used henceforth.

### Flashback Database
The flashback database feature allows rolling back an existing database or an existing backup to a specific point in time in the past, up to some maximum time history. For example, if a database is configured for flashback up to the last 25 hours, it should be possible to roll this database back to a point in time that is 25 hours ago. Also note that any backup taken from this database should preserve the same ability to rollback to a point in time.

Key points:
* This feature helps dealing with developer and operator error recovery mentioned in the Scenarios section, scenario A.
* The rollback would also include any DDL changes, such as create/drop/alter tables.
* The time granularity of the point in time that one can roll back to (1 second, 1 minute etc) is a separate parameter / specification.
* This feature does not help with reducing the size of backups, since this would be comparable to a full backup


### Cumulative and Differential Incremental Backups
Incremental backups only extract and backup the updates that occur after a specified point in time in the past. For example, an incremental might only contain all the changes that happened in the last hour. Note that the database should have been configured with the maximum history retention window (similar to the flashback database option). Therefore, if a database is configured to retain 25 hours of historical updates, then the largest incremental backup possible is 25 hours. Incremental backups should cover the following scenarios:
* All changes as a result of DML statements such as INSERT, UPDATE, DELETE
* DDL statements, such as creation of new tables and dropping of existing tables
* Should include updates for tables that may get dropped in that time interval

Key points:
* This feature helps dealing with developer and operator error recovery (mentioned in the Scenarios section A).
* The rollback should also include any DDL changes, such as create/drop/alter tables. 
* The time granularity of the point in time that one can roll back to (1 second, 1 minute etc) is a separate parameter / specification.
* Differential incremental backups require applying multiple incremental backups on top of a base backup

What’s different from flashback database option:
* Often run more frequently, since the data set size is reduced.
* This handles a DR scenario (see Scenarios section C).

There are two flavors of incremental backups, these are described below.
#### Cumulative incremental backups
Each of these incremental backups contain all changes since last base backup. The timestamp of the last base backup is specified by the operator. In this case, the point in time restore operation involves restoring the latest base backup followed by applying the latest cumulative incremental backup. 

#### Differential incremental backups
In this case, each incremental backup only contains the updates that occurred after the previous incremental backup. All changes since last incremental. A point in time restore operation in this case would involve restoring the latest base backup followed by applying every differential incremental backup taken since that base backup.

|                       | In-cluster flashback DB | Off-cluster flashback DB | Incremental backup | 
| --------------------- | ----------------------- | ------------------------ | ------------------ |
| Disk/file corruption  | Handled by replication in cluster | Handled by replication in cluster   | Handled by replication in cluster |
| App/operator eror     | Yes                     | Yes                      | Yes                |
| RPO                   | Very low                | High                     | Medium             |
| RTO                   | Very low                | High                     | High               |
| Disaster Recovery     | No (replication in cluster) | Yes                  | Yes                |
| Impact / Cost         | Very low                | High (snapshot and copy) | Medium             |


# Design

Handling incremental backups for point in time recovery will be done by impementing two separate features - flashback DB and incremental backups. As a distributed SQL database, the way the approach to accomplishing these features for table data differs from that for the table schema. In this section, we will discuss  the design for table data (or DML) and table schema (DDL) across flashback DB and incremental backups. 

> **Note:** that these depend on [performing a full snapshot / backup of the distributed database](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/distributed-backup-and-restore.md).

## Handling table data (DML changes)

The data in user table is split into tablets, each tablet peer stores data in the form of a set of WAL files (write ahead log, or the journal) and sst files (data files). Any update to the data is assigned a hybrid timestamp, which represent the timestamp when the update occurs. Note that this tablet data might reference transactions (in progress, committed and aborted), and requires the information in the transaction status table in order to be resolved correctly.

### Components of an incremental backup

Incremental backups would be performed using **distributed WAL archival**, where the older files in the write ahead log across various nodes in the database cluster are moved/hard-linked to a different filesystem - perhaps on a separate mount point (called an *archive location*). This serves as the location from which the WAL logs may be copied out remotely. This is the most realtime way of backing up data.

Based on the above, an incremental backup of the table data consists of the following:
* a set of WAL files of the tablets comprising the database/keyspace
* an optional set of sst data files (an optimization to reduce the data size and recovery time)
* the contents of the transaction status table

### Retaining intermediate updates with hybrid timestamps
Currently, the hybrid timestamp representing the timestamp of the update is written into the WAL for each update, and applied into the sst data files. However, older updates are dropped automatically by a process called compactions. Further, the hybrid timestamps are retained only for a short time period in the sst files. Recovering to a point in time in the past (called *history cutoff timestamp* requires that both the intermediate updates as well as the hydrid timestamps for each DML update be retained at least up to the *history cutoff timestamp*. For example, to allow recovering to any point in time over the last 24 hours retain all updates for the last 25 hours. This would require the following changes when PITR is enabled:
* Compactions should not purge any older values once updates to rows occur. They would continue to purge the older updates once it is outside the retention window.
* Every update should retain its corresponding hybrid timestamp.  


## Handling table schema (DDL changes)

DDL operations in a distributed database occur over a period of time, right from the time the operation is initiated by the user to the time where all changes have completed across all nodes in the cluster.


[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/design/point-in-time-recovery.md?pixel&useReferer)](https://github.com/yugabyte/ga-beacon)

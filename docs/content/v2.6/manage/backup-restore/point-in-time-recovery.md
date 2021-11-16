---
title: Point-in-Time Recovery
headerTitle: Point-in-time recovery
linkTitle: Point-in-time recovery
description: Restore data from a specific point in time in YugabyteDB
menu:
  v2.6:
    identifier: point-in-time-recovery
    parent: backup-restore
    weight: 704
isTocNested: true
showAsideToc: true
---

The point-in-time recovery feature allows you to restore the state of your cluster's data from a specific point in time. This can be relative, such as "three hours ago", or an absolute timestamp.

_Point-in-time recovery_ (also referred to here as PITR) and _incremental backups_ go hand in hand. These two features help in recovering from a number of error or failure scenarios by allowing the database to be restored to a specific point in time (in the past).

Point-in-time recoveries and incremental backups depend on _full backups_ (also referred to as base backups). A full backup, as the name suggests, is a complete transactional backup of data up to a certain point in time. The entire data set in the database is backed up for all of the namespaces and tables you selected. Full backups are resource-intensive, and can consume considerable amounts of CPU time, bandwidth, and disk space.

To learn more about YugabyteDB's point-in-time recovery feature, refer to the [Recovery scenarios](#recovery-scenarios), [Features](#features), [Use cases](#use-cases), and [Limitations](#limitations) sections on this page. For more details on the `yb-admin` commands, refer to the [Backup and snapshot commands](../../../admin/yb-admin#backup-and-snapshot-commands) section of the yb-admin documentation.

## Try out the PITR feature

There are several recovery scenarios [for YSQL](../../../explore/backup-restore/point-in-time-recovery-ysql/) and [for YCQL](../../../explore/backup-restore/point-in-time-recovery-ycql/) in the Explore section.

## Recovery scenarios

### App and operator errors

Point in time recovery allows recovery from the following scenarios by restoring the database to a point in time before the error occurred. The errors could be any of the following:

* DDL errors: For example, a table is dropped by mistake
* DML errors: For example, an erroneous UPDATE statement is run on the table

In both cases, you restore the table to a point in time before the error occurred.

### Disk or filesystem corruption

Data loss can happen due to one of the following reasons:

* Loss or failure of a disk
* Deletion of DB data files; for example, through operator error
* Bugs in the database software; for example, due to a software upgrade

In a distributed SQL database such as YugabyteDB, the first two scenarios can be mitigated due to the presence of live replicas, as it's highly unlikely the same issue occurs on all nodes. However, for the third scenario, point in time recovery is an important solution.

### Disasters

This is the scenario in which the data in the entire source cluster is lost irrecoverably, and a restore needs to be performed from a remote location. While the likelihood of this scenario is low, it's still important to understand the probability of correlated failures. For example, loss due to a natural disaster has a very low probability of occurrence in a multi-region deployment, but its probability increases with the proximity of the replicas.

## Features

{{< note title="Not all features are implemented yet" >}}

As this feature is in active development, not all features are implemented yet. Refer to the [Limitations](#limitations) section for details.

{{< /note >}}

This section describes the features that enable PITR and incremental backups.

### Flashback database

The flashback database feature allows restoring an existing database or an existing backup to a specific point in time in the past, up to some maximum time history. For example, if a database is configured for flashback up to the last 25 hours, you can restore this database back to a point in time that is up to 25 hours ago.

**Notes**:

* The time granularity of the point in time that one can restore to (1 second, 1 minute etc) is a separate parameter / specification.
* This feature does not help with reducing the size of backups, since this would be comparable to a full backup

### Incremental backups

Incremental backups only extract and backup the updates that occur after a specified point in time in the past. For example, all the changes that happened in the last hour. Note that the database should have been configured with the maximum history retention window (similar to the [flashback database](#flashback-database) option). Thus, if a database is configured to retain 25 hours of historical updates, then the largest possible incremental backup is 25 hours.

Incremental backups should cover the following scenarios:

* All changes as a result of DML statements such as INSERT, UPDATE, DELETE
* DDL statements, such as creation of new tables and dropping of existing tables
* Any updates for tables that may get dropped in that time interval

This feature helps dealing with developer and operator error recovery (mentioned in the Scenarios section A).
The restore should also include any DDL changes, such as create/drop/alter tables.
The time granularity of the point in time that one can restore to (1 second, 1 minute etc) is a separate parameter / specification.
Differential incremental backups require applying multiple incremental backups on top of a base backup

Compared to flashbacks, incremental backups:

* Often run more frequently, since the data set size is reduced.
* Can handle a disaster-recovery scenario.

There are two types of incremental backups, _differential_ and _cumulative_. Although YugayteDB supports both types, we recommend differential incremental backups.

#### Differential incremental backups

Each differential incremental backup only contains the updates that occurred after the previous incremental backup. All changes since last incremental. A point-in-time recovery operation in this case would involve restoring the latest base backup, followed by applying every differential incremental backup taken since that base backup.

#### Cumulative incremental backups

Each cumulative incremental backup contains all changes since the last base backup. The timestamp of the last base backup is specified by the operator. In this case, the point-in-time recovery operation involves restoring the latest base backup, followed by applying the latest cumulative incremental backup.

## Use cases

The following table provides a quick comparison of the intended usage patterns.

| Scenario | In-cluster flashback DB | Off-cluster flashback DB | Incremental backup |
| :------- | :---------------------- | :----------------------- | :----------------- |
| **Disk/file corruption** | Handled by replication in cluster | Handled by replication in cluster | Handled by replication in cluster |
| **App/operator error** | Yes | Yes | Yes |
| **RPO** | Very low | High | Medium |
| **RTO** | Very low | High | High |
| **Disaster Recovery** | No (replication in cluster) | Yes | Yes |
| **Impact / Cost** | Very low | High (snapshot and copy) | Medium |

## Limitations

This feature is in active development. YSQL and YCQL support different features, as detailed in the sections that follow.

### YSQL limitations

Currently, you can **restore data only**. The feature doesn't support metadata; in other words, restoring past operations such as CREATE, ALTER, TRUNCATE, and DROP TABLE is unsupported.

Development for this feature is tracked in [issue 7120](https://github.com/yugabyte/yugabyte-db/issues/7120). Support for undoing certain metadata operations is forthcoming.

### YCQL limitations

Currently, you can recover from the following YCQL operations:

* Data changes
* CREATE and DROP TABLE
* ALTER TABLE (including ADD, DROP, and RENAME COLUMN)
* CREATE and DROP INDEX

Development for this feature is tracked in [issue 7120](https://github.com/yugabyte/yugabyte-db/issues/7120). Some forthcoming features include:

* YCQL roles and permissions
* Support for automatic tablet splitting

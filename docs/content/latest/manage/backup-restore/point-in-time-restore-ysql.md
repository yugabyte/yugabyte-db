---
title: Point-in-Time Restore for YSQL
headerTitle: Point-in-time restore
linkTitle: Point-in-time restore
description: Restore data from a specific point in time in YugabyteDB for YSQL
beta: /latest/faq/general/#what-is-the-definition-of-the-beta-feature-tag
aliases:
menu:
  latest:
    identifier: point-in-time-restore
    parent: backup-restore
    weight: 704
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/latest/manage/backup-restore/point-in-time-restore-ysql" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="/latest/manage/backup-restore/point-in-time-restore-ycql" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

The BETA point-in-time restore feature allows you to restore the state of your cluster's data from a specific point in time. This can be relative, such as "three hours ago", or an absolute timestamp.

_Point-in-time restores_ (also referred to here as PITR) and _incremental backups_ go hand in hand. These two features help in recovering from a number of error or failure scenarios by allowing the database to be rolled back to a specific point in time (in the past). The rollback starts from the last full backup, and replays all of the updates from that point until the requested restore point.

Point-in-time restores and incremental backups depend on _full backups_ (also referred to as base backups). A full backup, as the name suggests, is a complete transactional backup of data up to a certain point in time. The entire data set in the database is backed up for all of the namespaces and tables you selected. Full backups are resource-intensive, and can consume considerable amounts of CPU time, bandwidth, and disk space.

To learn more about YugabyteDB's point-in-time restore feature, refer to the [Recovery scenarios](#recovery-scenarios), [Features](#features), [Use cases](#use-cases), and [Limitations](#limitations) sections on this page.

## Try out the PITR feature

You can test the PITR feature by creating a database and populating it, creating a snapshot, and restoring ([data only](#limitations)!) from that snapshot.

### Create and snapshot a table

Create and populate a table, look at a timestamp to which you'll restore, and then write a row.

1. Start the YSQL shell and connect to your local instance:

    ```sh
    $ bin/ysqlsh -h 127.0.0.1
    ```

1. Create a table and populate some sample data:

    ```sql
    CREATE TABLE employees (
      employee_no integer PRIMARY KEY,
      name text,
      department text,
      salary integer
    );

    INSERT INTO employees (employee_no, name, department, salary) 
      VALUES 
      (1221, 'John Smith', 'Marketing', 50000),
      (1222, 'Bette Davis', 'Sales', 55000),
      (1223, 'Lucille Ball', 'Operations', 70000),
      (1224, 'John Zimmerman', 'Sales', 60000);
    
    SELECT * from employees;
    ```

    ```output
    employee_no |      name      | department | salary 
    -------------+----------------+------------+--------
            1223 | Lucille Ball   | Operations |  70000
            1224 | John Zimmerman | Sales      |  60000
            1221 | John Smith     | Marketing  |  50000
            1222 | Bette Davis    | Sales      |  55000
    (4 rows)
    ```

1. At a terminal prompt, create a snapshot of the table from a shell prompt:

    ```sh
    $ bin/yb-admin create_database_snapshot yugabyte
    ```

    ```output
    Started snapshot creation: bb5fc435-a2b9-4f3a-a510-0bacc6aebccf
    ```

1. Verify that the snapshot is complete:

    ```sh
    $ bin/yb-admin list_snapshots
    ```

    ```output
    Snapshot UUID                         State
    bb5fc435-a2b9-4f3a-a510-0bacc6aebccf  COMPLETE
    No snapshot restorations
    ```

### Restore from an absolute time

1. From the YSQL shell, get a timestamp. You can also use a [YCQL timestamp](../../../api/ycql/type_datetime/#timestamp) with the restore command, if you like.

    <br/>

    Note: YSQL timestamps are in seconds, with 5 digits after the decimal point. You'll need to multiply the result by 1,000,000 (1 million) later, to use it with the yb-admin command-line tools.

    ```sql
    yugabyte=# select extract(epoch from now()) as current_timestamp;
    ```

    ```output
    current_timestamp 
    ------------------
     1617670679.18510
    (1 row)
    ```

1. Add a row for employee 9999 to the table:

    ```sql
    INSERT INTO employees (employee_no, name, department, salary) 
      VALUES 
      (9999, 'Wrong Name', 'Marketing', 10000);

    SELECT * FROM employees;
    ```

    ```output
    employee_no |      name      | department | salary 
    -------------+----------------+------------+--------
            1223 | Lucille Ball   | Operations |  70000
            9999 | Wrong Name     | Marketing  |  10000
            1224 | John Zimmerman | Sales      |  60000
            1221 | John Smith     | Marketing  |  50000
            1222 | Bette Davis    | Sales      |  55000
    (5 rows)
    ```

1. At a terminal prompt, list snapshots:

    ```sh
    $ bin/yb-admin list_snapshots
    ```

    ```output
    Snapshot UUID                         State
    bb5fc435-a2b9-4f3a-a510-0bacc6aebccf  COMPLETE
    No snapshot restorations
    ```

1. Restore the latest snapshot to the timestamp you obtained before you deleted the data:

    <br/>

    Multiply the timestamp you obtained earlier by 1,000,000 (1 million). A shortcut is to remove the decimal point, and add a zero (0) to the end. So, the example earlier of `1617670679.18510` becomes `1617670679185100`.

    ```sh
    $ bin/yb-admin restore_snapshot bb5fc435-a2b9-4f3a-a510-0bacc6aebccf 1617670679185100
    ```

1. Verify the restoration is in `RESTORED` state:

    ```sh
    $ bin/yb-admin list_snapshots
    ```

    ```output
    Snapshot UUID                     State
    bb5fc435-a2b9-4f3a-a510-0bacc6aebccf  COMPLETE
    Restoration UUID                      State
    bd7e4e52-b763-4b95-87ce-9399e1ac206e  RESTORED
    ```

1. In the YSQL shell, verify the data is restored, without a row for employee 9999:

    ```sql
    yugabyte=# select * from employees;
    ```

    ```output
    employee_no |      name      | department | salary 
    -------------+----------------+------------+--------
            1223 | Lucille Ball   | Operations |  70000
            1224 | John Zimmerman | Sales      |  60000
            1221 | John Smith     | Marketing  |  50000
            1222 | Bette Davis    | Sales      |  55000
    (4 rows)
    ```

### Restore from a relative time

In addition to restoring to a particular timestamp, you can also restore from a relative time, such as "ten minutes ago". In this example, you'll delete some data from the existing `employees` table, then restore the state of the database to what it was five minutes prior.

When you specify a relative time, you can specify any or all of _days_, _hours_, _minutes_, and _seconds_. For example:

* `"5m"` to restore from five minutes ago
* `"1h"` to restore from one hour ago
* `"3d"` to restore from three days ago
* `"1h 5m"` to restore from one hour and five minutes ago

Relative times can be in any of the following formats (again, note that you can specify any or all of days, hours, minutes, and seconds):

* ISO 8601: `3d 4h 5m 6s`
* Abbreviated PostgreSQL: `3 d 4 hrs 5 mins 6 secs`
* Traditional PostgreSQL: `3 days 4 hours 5 minutes 6 seconds`
* SQL standard: `D H:M:S`

**Careful!** If you specify a time prior to when you created the table, the restore will leave the table intact, but empty.

1. Wait at least five minutes after you complete the steps in the previous section. This is so that you can use a known relative time for the restore.

1. In the YSQL shell, remove employee 1223 from the table:

    ```sql
    yugabyte=# delete from employees where employee_no=1223;

    yugabyte=# select * from employees;
    ```

    ```output
     employee_no |      name      | department | salary 
    -------------+----------------+------------+--------
            1224 | John Zimmerman | Sales      |  60000
            1221 | John Smith     | Marketing  |  50000
            1222 | Bette Davis    | Sales      |  55000
    (3 rows)
    ```

1. At a terminal prompt, restore the snapshot you created earlier:

    ```sh
    $ bin/yb-admin restore_snapshot bb5fc435-a2b9-4f3a-a510-0bacc6aebccf minus "5m"
    ```

1. Verify the restoration is in `RESTORED` state:

    ```sh
    $ bin/yb-admin list_snapshots
    ```

    ```output
    Snapshot UUID                     State
    bb5fc435-a2b9-4f3a-a510-0bacc6aebccf  COMPLETE
    Restoration UUID                      State
    bd7e4e52-b763-4b95-87ce-9399e1ac206e  RESTORED
    ```

1. Verify the data is restored, with a row for employee 1223:

    ```sql
    yugabyte=# select * from employees;
    ```

    ```output
    employee_no |      name      | department | salary 
    -------------+----------------+------------+--------
            1223 | Lucille Ball   | Operations |  70000
            1224 | John Zimmerman | Sales      |  60000
            1221 | John Smith     | Marketing  |  50000
            1222 | Bette Davis    | Sales      |  55000
    (4 rows)
    ```

## Recovery scenarios

### App and operator errors

Point in time recovery allows recovery from the following scenarios by rolling the database back to a point in time before the error occurred. The errors could be any of the following:

* DDL errors: A table is dropped by mistake
* DML errors: An erroneous UPDATE statement is run on the table

In both cases, you roll the table back to a point in time before the error occurred.

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

As this is a BETA feature in active development, not all features are implemented yet. Refer to the [Limitations](#limitations) section for details.

{{< /note >}}

This section describes the features that enable PITR and incremental backups.

### Flashback database

The flashback database feature allows rolling back an existing database or an existing backup to a specific point in time in the past, up to some maximum time history. For example, if a database is configured for flashback up to the last 25 hours, you can roll this database back to a point in time that is up to 25 hours ago. Any backups taken from this database preserve the same ability to rollback to a point in time.

**Notes**:

* The time granularity of the point in time that one can roll back to (1 second, 1 minute etc) is a separate parameter / specification.
* This feature does not help with reducing the size of backups, since this would be comparable to a full backup

### Incremental backups

Incremental backups only extract and backup the updates that occur after a specified point in time in the past. For example, all the changes that happened in the last hour. Note that the database should have been configured with the maximum history retention window (similar to the [flashback database](#flashback-database) option). Thus, if a database is configured to retain 25 hours of historical updates, then the largest possible incremental backup is 25 hours.

Incremental backups should cover the following scenarios:

* All changes as a result of DML statements such as INSERT, UPDATE, DELETE
* DDL statements, such as creation of new tables and dropping of existing tables
* Any updates for tables that may get dropped in that time interval

This feature helps dealing with developer and operator error recovery (mentioned in the Scenarios section A).
The rollback should also include any DDL changes, such as create/drop/alter tables.
The time granularity of the point in time that one can roll back to (1 second, 1 minute etc) is a separate parameter / specification.
Differential incremental backups require applying multiple incremental backups on top of a base backup

Compared to flashbacks, incremental backups:

* Often run more frequently, since the data set size is reduced.
* Can handle a disaster-recovery scenario.

There are two types of incremental backups, _differential_ and _cumulative_. Although YugayteDB supports both types, we recommend differential incremental backups.

#### Differential incremental backups

Each differential incremental backup only contains the updates that occurred after the previous incremental backup. All changes since last incremental. A point-in-time restore operation in this case would involve restoring the latest base backup, followed by applying every differential incremental backup taken since that base backup.

#### Cumulative incremental backups

Each cumulative incremental backup contains all changes since the last base backup. The timestamp of the last base backup is specified by the operator. In this case, the point-in-time restore operation involves restoring the latest base backup, followed by applying the latest cumulative incremental backup.

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

This is a BETA feature, and is in active development. Currently, you can **restore data only**. The feature doesn't support metadata; in other words, rolling back past operations such as CREATE, ALTER, TRUNCATE, and DROP TABLE is unsupported.

Development for this feature is tracked in [issue 7120](https://github.com/yugabyte/yugabyte-db/issues/7120). Some forthcoming features include:

* Automatic configuration
* Support for undoing metadata operations, such as CREATE, ALTER, TRUNCATE, or DROP TABLE
* Schedules to take snapshots at user-defined intervals
* Options to restore with different granularities, such as a single YSQL database or the whole YCQL dataset.

---
title: Point-in-Time Restore for YCQL
headerTitle: Point-in-time restore
linkTitle: Point-in-time restore
description: Restore data from a specific point in time in YugabyteDB for YCQL
beta: /latest/faq/general/#what-is-the-definition-of-the-beta-feature-tag
aliases:
menu:
  latest:
    identifier: point-in-time-restore-2-ycql
    parent: backup-restore
    weight: 704
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/latest/manage/backup-restore/point-in-time-restore-ysql" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="/latest/manage/backup-restore/point-in-time-restore-ycql" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

{{< note>}}
Refer to the [YSQL tab](../point-in-time-restore-ysql) for details on this feature.
{{</ note >}}

## Try out the PITR feature

You can test the PITR feature (BETA) by creating a database and populating it, creating a snapshot, and restoring (be sure to check out the [limitations](#limitations)!) from that snapshot.

{{< tip title="Examples are simplified" >}}

The examples on this page are deliberately simple. In many of the scenarios presented, you could drop the index or table to recover. Consider the examples as part of an effort to undo a larger schema change, such as a database migration, which has performed several operations.

{{< /tip >}}

### Create and snapshot a table

Create and populate a table, look at a timestamp to which you'll restore, and then write a row.

1. Start the YCQL shell and connect to your local instance:

    ```sh
    $ bin/ycqlsh
    ```

1. Create a table and populate some sample data:

    ```sql
    create keyspace pitr;

    use pitr;

    create table employees (
      employee_no integer PRIMARY KEY,
      name text,
      department text,
      salary integer
    ) with transactions = { 'enabled' : true };

    insert into employees (employee_no, name, department, salary) values (1221, 'John Smith', 'Marketing', 50000);
    insert into employees (employee_no, name, department, salary) values (1222, 'Bette Davis', 'Sales', 55000);
    insert into employees (employee_no, name, department, salary) values (1223, 'Lucille Ball', 'Operations', 70000);
    insert into employees (employee_no, name, department, salary) values (1224, 'John Zimmerman', 'Sales', 60000);

    SELECT * from employees;
    ```

    ```output
     employee_no | name           | department | salary
    -------------+----------------+------------+--------
            1223 |   Lucille Ball | Operations |  70000
            1224 | John Zimmerman |      Sales |  60000
            1221 |     John Smith |  Marketing |  50000
            1222 |    Bette Davis |      Sales |  55000

    (4 rows)
    ```

1. Create a snapshot of the table from a shell prompt:

    ```sh
    $ bin/yb-admin create_database_snapshot pitr employees
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

1. Get a timestamp. YCQL doesn't have a `now()` function, so use a command such as one of the following. You can also use a [YCQL timestamp](../../../api/ycql/type_datetime/#timestamp) with the restore command, if you like.

    ```sh
    # Ruby: remove the decimal point before using the timestamp
    $ ruby -e 'puts Time.now.to_f'
    ```

    ```output
    1617818825.646913
    ```

    ```sh
    # Python: remove the decimal point before using the timestamp
    $ python -c 'import datetime; print datetime.datetime.now().strftime("%s.%f")'
    ```

    ```output
    1617818868.669611
    ```

    ```sh
    # Linux and some other systems (but not macOS): use the timestamp as-is
    $ date +%s%N | cut -b1-16
    ```

    ```output
    1617818943892323
    ```

1. Add a row for employee 9999 to the table:

    ```sql
    insert into employees (employee_no, name, department, salary) values (9999, 'Wrong Name', 'Marketing', 10000);

    select * from employees;
    ```

    ```output
     employee_no | name           | department | salary
    -------------+----------------+------------+--------
            1223 |   Lucille Ball | Operations |  70000
            9999 |     Wrong Name |  Marketing |  10000
            1224 | John Zimmerman |      Sales |  60000
            1221 |     John Smith |  Marketing |  50000
            1222 |    Bette Davis |      Sales |  55000

    (5 rows)
    ```

1. List snapshots, at a terminal prompt:

    ```sh
    $ bin/yb-admin list_snapshots
    ```

    ```output
    Snapshot UUID                         State
    bb5fc435-a2b9-4f3a-a510-0bacc6aebccf  COMPLETE
    No snapshot restorations
    ```

1. Restore the latest snapshot to the timestamp you obtained before you deleted the data, at a terminal prompt:

    ```sh
    $ bin/yb-admin restore_snapshot bb5fc435-a2b9-4f3a-a510-0bacc6aebccf 1617818943892323

1. Next, verify the restoration is in `RESTORED` state:

    ```sh
    $ bin/yb-admin list_snapshots
    ```

    ```output
    Snapshot UUID                         State
    bb5fc435-a2b9-4f3a-a510-0bacc6aebccf  COMPLETE
    Restoration UUID                      State
    bd7e4e52-b763-4b95-87ce-9399e1ac206e  RESTORED
    ```

1. In the YCQL shell, verify the data is restored, and there is no row for employee 9999:

    ```sql
    ycqlsh:pitr> select * from employees;
    ```

    ```output
     employee_no | name           | department | salary
    -------------+----------------+------------+--------
            1223 |   Lucille Ball | Operations |  70000
            1224 | John Zimmerman |      Sales |  60000
            1221 |     John Smith |  Marketing |  50000
            1222 |    Bette Davis |      Sales |  55000

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

1. Wait at least five minutes after you complete the steps in the previous section. This is so that you can easily use a known relative time for the restore.

1. From the YCQL shell, remove employee 1223 from the table:

    ```sql
    ycqlsh:pitr> delete from employees where employee_no=1223;

    ycqlsh:pitr> select * from employees;
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
    Snapshot UUID                         State
    bb5fc435-a2b9-4f3a-a510-0bacc6aebccf  COMPLETE
    Restoration UUID                      State
    bd7e4e52-b763-4b95-87ce-9399e1ac206e  RESTORED
    ```

1. Verify the data is restored, and employee 1223 is back:

    ```sql
    ycqlsh:pitr> select * from employees;
    ```

    ```output
     employee_no | name           | department | salary
    -------------+----------------+------------+--------
            1223 |   Lucille Ball | Operations |  70000
            1224 | John Zimmerman |      Sales |  60000
            1221 |     John Smith |  Marketing |  50000
            1222 |    Bette Davis |      Sales |  55000

    (4 rows)
    ```

## Undo metadata changes

In addition to data changes, you can also use PITR to recover from metadata changes, such as creating and altering tables, and creating indexes.

### Undo table creation

1. Using the same keyspace as the previous scenarios, create a new table.

    ```sql
    create table t2(k int primary key);
    ```

1. Now restore back to a time before this table was created, as in [Restore from a relative time](#restore-from-a-relative-time).

1. Due to a ycqlsh caching issue, to see the effect of this change, you will need to drop out of your current ycqlsh session and log back in.

1. Check to see that table t2 is gone.

    ```sh
    ./bin/ycqlsh -e 'use pitr; describe tables;'
    ```

    ```output
    employees
    ```

### Undo table alteration

#### Undo ADD COLUMN

1. Using the same keyspace as the previous scenarios, alter your table by adding a column.

    ```sql
    alter table pitr.employees add v2 int;
    select * from pitr.employees;
    ```

    ```output
     employee_no | name           | department | salary |  v2
    -------------+----------------+------------+--------+------
            1223 |   Lucille Ball | Operations |  70000 | null
            1224 | John Zimmerman |      Sales |  60000 | null
            1221 |     John Smith |  Marketing |  50000 | null
            1222 |    Bette Davis |      Sales |  55000 | null

    (4 rows)
    ```

1. Now restore back to a time before this table was altered, as in [Restore from a relative time](#restore-from-a-relative-time).

1. Due to a ycqlsh caching issue, to see the effect of this change, you will need to drop out of your current ycqlsh session and log back in.

1. Check to see that the v2 column is gone.

    ```sql
    ycqlsh:pitr> select * from employees;
    ```

    ```output
     employee_no | name           | department | salary
    -------------+----------------+------------+--------
            1223 |   Lucille Ball | Operations |  70000
            1224 | John Zimmerman |      Sales |  60000
            1221 |     John Smith |  Marketing |  50000
            1222 |    Bette Davis |      Sales |  55000

    (4 rows)
    ```

#### Undo DROP COLUMN

1. Using the same keyspace as the previous scenarios, alter your table by dropping a column.

    ```sql
    alter table pitr.employees drop salary;
    select * from pitr.employees;
    ```

    ```output
     employee_no | name           | department
    -------------+----------------+-----------
            1223 |   Lucille Ball | Operations
            1224 | John Zimmerman |      Sales
            1221 |     John Smith |  Marketing
            1222 |    Bette Davis |      Sales

    (4 rows)
    ```

1. Now restore back to a time before this table was altered, as in [Restore from a relative time](#restore-from-a-relative-time).

1. Due to a ycqlsh caching issue, to see the effect of this change, you will need to drop out of your current ycqlsh session and log back in.

1. Check to see that the salary column is back.

    ```sql
    ycqlsh:pitr> select * from employees;
    ```

    ```output
     employee_no | name           | department | salary
    -------------+----------------+------------+--------
            1223 |   Lucille Ball | Operations |  70000
            1224 | John Zimmerman |      Sales |  60000
            1221 |     John Smith |  Marketing |  50000
            1222 |    Bette Davis |      Sales |  55000

    (4 rows)
    ```

### Undo index creation

1. Create an index on the table from the previous examples.

    ```sql
    use pitr;
    create index t1_index on employees (employee_no);
    describe index t1_index;
    ```

    ```output
    CREATE INDEX t1_index ON pitr.employees (employee_no)
    WITH transactions = {'enabled': 'true'};
    ```

1. Now restore back to a time before this index was created.

    **Note**: Due to a ycqlsh caching issue, to see the effect of this change, you will need to drop out of your current ycqlsh session and log back in.

    ```sh
    ./bin/ycqlsh -e 'use pitr; describe table pitr.t1_index;'
    ```

    ```output
    <stdin>:1:Column family u't1_index' not found
    ```

## Limitations

This is a BETA feature, and is in active development. Currently, you can recover from the following operations:

* Data changes
* CREATE INDEX
* CREATE TABLE
* ALTER TABLE (including ADD and DROP COLUMN)
* DROP TABLE

Development for this feature is tracked in [issue 7120](https://github.com/yugabyte/yugabyte-db/issues/7120). Some forthcoming features include:

* Automatic configuration
* Options to restore with different granularities, such as a single YSQL database or the whole YCQL dataset.

---
title: Point-in-Time Restore for YCQL (BETA)
headerTitle: Point in time restore
linkTitle: Point in time restore
description: Restore data from a specific point in time in YugabyteDB for YCQL (BETA)
aliases:
menu:
  latest:
    identifier: point-in-time-restore-ycql
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

At this point, you can test the PITR feature by creating a database and populating it, creating a snapshot, and restoring ([data only](#limitations)!) from that snapshot.

Let's get started:

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
    );

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

1. Get a timestamp. YCQL doesn't have a now() function, so use a command such as one of the following:

    ```sh
    # Ruby: remove the decimal point before using the timestamp
    $ ruby -e 'puts Time.now.to_f'
    ```

    ```sh
    # Python: remove the decimal point before using the timestamp
    $ python -c 'import datetime; print datetime.datetime.now().strftime("%s.%f")'
    ```

    ```sh
    # Linux and some other systems (but not macOS): use the timestamp as-is
    $ date +%s%N | cut -b1-15
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
    $ bin/yb-admin restore_snapshot bb5fc435-a2b9-4f3a-a510-0bacc6aebccf 1617670679185100

1. Verify the snapshot is restored, at a terminal prompt:

    ```sh
    $ bin/yb-admin list_snapshots
    ```

    ```output
    Snapshot UUID                     State
    bb5fc435-a2b9-4f3a-a510-0bacc6aebccf  COMPLETE
    Restoration UUID                      State
    bd7e4e52-b763-4b95-87ce-9399e1ac206e  RESTORED
    ```

1. Verify the data is restored:

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

In addition to restoring to a particular timestamp, you can also restore to a relative time, such as "ten minutes ago". In this example, you'll delete some data from the existing `employees` table, then restore the state of the database to what it was five minutes prior.

1. Wait five minutes after you complete the steps in the previous section. This is so that you can easily use a known relative time for the restore.

1. Remove employee 1223 from the table:

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
    (4 rows)
    ```

1. Restore the snapshot you created earlier:

    ```sh
    $ bin/yb-admin restore_snapshot bb5fc435-a2b9-4f3a-a510-0bacc6aebccf "minus 3m"
    ```

1. Verify the data is restored:

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

## Limitations

This is a BETA feature, and is in active development. Currently, you can **restore data only**. The feature doesn't support metadata; in other words, it won't currently roll back operations such as CREATE, ALTER, TRUNCATE, and DROP TABLE.

Development for this feature is tracked in [issue 7120](https://github.com/yugabyte/yugabyte-db/issues/7120). Some forthcoming features include:

* Automatic configuration
* Early metadata support, such as undoing CREATE TABLE operations
* More complete metadata support: TRUNCATE, DROP, ALTER
* Per-database restore for YSQL

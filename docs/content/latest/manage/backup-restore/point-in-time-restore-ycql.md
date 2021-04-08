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

You can test the PITR feature (BETA) by creating a database and populating it, creating a snapshot, and restoring ([data only](#limitations)!) from that snapshot.

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
    Snapshot UUID                     State
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
    Snapshot UUID                     State
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

## Limitations

This is a BETA feature, and is in active development. Currently, you can **restore data only**. The feature doesn't support metadata; in other words, rolling back past operations such as CREATE, ALTER, TRUNCATE, and DROP TABLE is unsupported.

Development for this feature is tracked in [issue 7120](https://github.com/yugabyte/yugabyte-db/issues/7120). Some forthcoming features include:

* Automatic configuration
* Support for undoing metadata operations, such as CREATE, ALTER, TRUNCATE, or DROP TABLE
* Schedules to take snapshots at user-defined intervals
* Options to restore with different granularities, such as a single YSQL database or the whole YCQL dataset.

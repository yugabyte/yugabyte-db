---
title: Point-in-Time Recovery for YSQL
headerTitle: Point-in-time recovery
linkTitle: Point-in-time recovery
description: Restore data from a specific point in time in YugabyteDB for YSQL
menu:
  v2.18:
    identifier: cluster-management-point-in-time-recovery
    parent: explore-cluster-management
    weight: 704
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../point-in-time-recovery-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../point-in-time-recovery-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

Point-in-time recovery (PITR) allows you to restore the state of your cluster's data and [certain types](../../../manage/backup-restore/point-in-time-recovery/#limitations) of metadata from a specific point in time. This can be relative, such as "three hours ago", or an absolute timestamp.

For more information, see [Point-in-time recovery](../../../manage/backup-restore/point-in-time-recovery/#features). For details on the `yb-admin` commands, refer to the [Backup and snapshot commands](../../../admin/yb-admin/#backup-and-snapshot-commands) section of the yb-admin documentation.

The following examples show how you can use the PITR feature by creating a database and populating it, creating a snapshot schedule, and restoring from a snapshot on the schedule.

Note that the examples are deliberately simplified. In many of the scenarios, you could drop the index or table to recover. Consider the examples as part of an effort to undo a larger schema change, such as a database migration, which has performed several operations.

## Set up universe

The examples run on a local multi-node YugabyteDB universe. To create a universe, see [Set up YugabyteDB universe](../../#set-up-yugabytedb-universe).

## Undo data changes

The process of undoing data changes involves creating and taking a snapshot of a table, and then performing a restore from either an absolute or relative time.

Before attempting a restore, you need to confirm that there is no restore in progress for the subject keyspace or table; if multiple restore commands are issued, the data might enter an inconsistent state. For details, see [Restore to a point in time](../../../manage/backup-restore/point-in-time-recovery/#restore-to-a-point-in-time).

### Create a table

1. Start the YSQL shell and connect to your local instance:

    ```sh
    ./bin/ysqlsh -h 127.0.0.1
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

### Create a snapshot

Create a snapshot as follows:

1. At a terminal prompt, create a snapshot schedule for the database from a shell prompt. In the following example, the schedule is one snapshot every minute, and each snapshot is retained for ten minutes:

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        create_snapshot_schedule 1 10 ysql.yugabyte
    ```

    ```output.json
    {
        "schedule_id": "0e4ceb83-fe3d-43da-83c3-013a8ef592ca"
    }
    ```

1. Verify that a snapshot has happened:

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        list_snapshot_schedules
    ```

    ```output.json
    {
        "schedules": [
            {
                "id": "0e4ceb83-fe3d-43da-83c3-013a8ef592ca",
                "options": {
                    "interval": "60.000s",
                    "retention": "600.000s"
                },
                "snapshots": [
                    {
                        "id": "8d588cb7-13f2-4bda-b584-e9be47a144c5",
                        "snapshot_time_utc": "2021-05-07T20:16:08.492330+0000"
                    }
                ]
            }
        ]
    }
    ```

### Restore from an absolute time

1. From a command prompt, get a timestamp:

    ```sh
    python -c 'import datetime; print(datetime.datetime.now().strftime("%s%f"))'
    ```

    ```output
    1620418817729963
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

1. Restore the snapshot schedule to the timestamp you obtained before you added the data, at a terminal prompt:

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        restore_snapshot_schedule 0e4ceb83-fe3d-43da-83c3-013a8ef592ca 1620418817729963
    ```

    ```output.json
    {
        "snapshot_id": "2287921b-1cf9-4bbc-ad38-e309f86f72e9",
        "restoration_id": "1c5ef7c3-a33a-46b5-a64e-3fa0c72709eb"
    }
    ```

1. Next, verify the restoration is in `RESTORED` state (you'll see more snapshots in the list, as well):

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        list_snapshots
    ```

    ```output
    Snapshot UUID                           State       Creation Time
    8d588cb7-13f2-4bda-b584-e9be47a144c5    COMPLETE    2023-04-20 00:24:58.246932
    1f4db0e2-0706-45db-b157-e577702a648a    COMPLETE    2023-04-20 00:26:03.257519
    b91c734b-5c57-4276-851e-f982bee73322    COMPLETE    2023-04-20 00:27:08.272905
    04fc6f05-8775-4b43-afbd-7a11266da110    COMPLETE    2023-04-20 00:28:13.287202
    e7bc7b48-351b-4713-b46b-dd3c9c028a79    COMPLETE    2023-04-20 00:29:18.294031
    2287921b-1cf9-4bbc-ad38-e309f86f72e9    COMPLETE    2023-04-20 00:30:23.306355
    97aa2968-6b56-40ce-b2c5-87d2e54e9786    COMPLETE    2023-04-20 00:31:28.319685
    Restoration UUID                        State
    1c5ef7c3-a33a-46b5-a64e-3fa0c72709eb    RESTORED
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

In addition to restoring to a particular timestamp, you can also restore from a relative time, such as "ten minutes ago".

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

Refer to the yb-admin [_restore-snapshot-schedule_ command](../../../admin/yb-admin/#restore-snapshot-schedule) for more details.

## Undo metadata changes

In addition to data changes, you can also use PITR to recover from metadata changes, such as creating, altering, and deleting tables and indexes.

Before you begin, if a local universe is currently running, first [destroy it](../../../reference/configuration/yugabyted/#destroy-a-local-cluster), and create a local multi-node YugabyteDB universe as described in [Set up YugabyteDB universe](../../#set-up-yugabytedb-universe).

### Undo table creation

1. At a terminal prompt, create a snapshot schedule for the database. In this example, the schedule is on the default `yugabyte` database, one snapshot every minute, and each snapshot is retained for ten minutes:

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        create_snapshot_schedule 1 10 ysql.yugabyte
    ```

    ```output.json
    {
      "schedule_id": "1fb2d85a-3608-4cb1-af63-3e4062300dc1"
    }
    ```

1. Verify that a snapshot has happened:

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        list_snapshot_schedules
    ```

    ```output.json
    {
        "schedules": [
            {
                "id": "1fb2d85a-3608-4cb1-af63-3e4062300dc1",
                "options": {
                    "filter": "ysql.yugabyte",
                    "interval": "1 min",
                    "retention": "10 min"
                },
                "snapshots": [
                    {
                        "id": "34b44c96-c340-4648-a764-7965fdcbd9f1",
                        "snapshot_time": "2023-04-20 00:20:38.214201"
                    }
                ]
            }
        ]
    }
    ```

1. To restore from an absolute time, get a timestamp from the command prompt. You'll create a table, then restore to this time to undo the table creation:

    ```sh
    python -c 'import datetime; print(datetime.datetime.now().strftime("%s%f"))'
    ```

    ```output
    1681964544554620
    ```

1. Start the YSQL shell and create a table as described in [Create a table](#create-a-table).

1. Restore the snapshot schedule to the timestamp you obtained before you created the table, at a terminal prompt:

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        restore_snapshot_schedule 1fb2d85a-3608-4cb1-af63-3e4062300dc1 1681964544554620
    ```

    ```output.json
    {
        "snapshot_id": "0f1582ea-c10d-4ad9-9cbf-e2313156002c",
        "restoration_id": "a61046a2-8b77-4d6e-87e1-1dc44b5ebc69"
    }
    ```

1. Verify the restoration is in `RESTORED` state (you'll see more snapshots in the list, as well):

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        list_snapshots
    ```

    ```output
    Snapshot UUID                           State       Creation Time
    34b44c96-c340-4648-a764-7965fdcbd9f1    COMPLETE    2023-04-20 00:20:38.214201
    bacd0b53-6a51-4628-b898-e35116860735    COMPLETE    2023-04-20 00:21:43.221612
    0f1582ea-c10d-4ad9-9cbf-e2313156002c    COMPLETE    2023-04-20 00:22:48.231456
    617f9df8-3087-4b04-9187-399b52e738ee    COMPLETE    2023-04-20 00:23:53.239147
    489e6903-2848-478b-9519-577084e49adf    COMPLETE    2023-04-20 00:24:58.246932
    Restoration UUID                        State
    a61046a2-8b77-4d6e-87e1-1dc44b5ebc69    RESTORED
    ```

1. Verify that the table no longer exists:

    ```sh
    ./bin/ysqlsh -d yugabyte;
    ```

    ```sql
    \d employees;
    ```

    ```output
    Did not find any relation named "employees".
    ```

### Undo table deletion

1. Start the YSQL shell and create a table as described in [Create a table](#create-a-table).

1. Verify that a snapshot has happened since table creation:

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        list_snapshot_schedules
    ```

    ```output.json
    {
        "schedules": [
            {
                "id": "1fb2d85a-3608-4cb1-af63-3e4062300dc1",
                "options": {
                    "filter": "ysql.yugabyte",
                    "interval": "1 min",
                    "retention": "10 min"
                },
                "snapshots": [
                    {
                        "id": "34b44c96-c340-4648-a764-7965fdcbd9f1",
                        "snapshot_time": "2023-04-20 00:20:38.214201"
                    },
                    {
                        "id": "bacd0b53-6a51-4628-b898-e35116860735",
                        "snapshot_time": "2023-04-20 00:21:43.221612",
                        "previous_snapshot_time": "2023-04-20 00:20:38.214201"
                    },
                    [...]
                    {
                        "id": "c98c890a-97ae-49f0-9c73-8d27c430874f",
                        "snapshot_time": "2023-04-20 00:28:13.287202",
                        "previous_snapshot_time": "2023-04-20 00:27:08.272905"
                    }
                ]
            }
        ]
    }
    ```

1. To restore from an absolute time, get a timestamp from the command prompt. You'll delete the table, then restore to this time to undo the delete:

    ```sh
    python -c 'import datetime; print(datetime.datetime.now().strftime("%s%f"))'
    ```

    ```output
    1681965106732671
    ```

1. In ysqlsh, drop this table:

    ```sql
    drop table employees;
    ```

    ```output
    DROP TABLE
    ```

1. Restore the snapshot schedule to the timestamp you obtained before you deleted the table, at a terminal prompt:

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        restore_snapshot_schedule 1fb2d85a-3608-4cb1-af63-3e4062300dc1 1681965106732671
    ```

    ```output.json
    {
        "snapshot_id": "fc95304a-b713-4468-a128-d5155c85333a",
        "restoration_id": "2bc005ca-c842-4c7c-9cc7-34e1f75ca467"
    }
    ```

1. Verify the restoration is in `RESTORED` state (you'll see more snapshots in the list, as well):

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        list_snapshots
    ```

    ```output
    Snapshot UUID                           State       Creation Time
    489e6903-2848-478b-9519-577084e49adf    COMPLETE    2023-04-20 00:24:58.246932
    e4c12e39-6b15-49f2-97d1-86f777650d6b    COMPLETE    2023-04-20 00:26:03.257519
    3d1176d0-f56d-44f3-bb29-2fcb9b08186b    COMPLETE    2023-04-20 00:27:08.272905
    c98c890a-97ae-49f0-9c73-8d27c430874f    COMPLETE    2023-04-20 00:28:13.287202
    17e9c8f7-2965-48d0-8459-c9dc90b8ed93    COMPLETE    2023-04-20 00:29:18.294031
    e1900004-9a89-4c3a-b60b-4b570058c4da    COMPLETE    2023-04-20 00:30:23.306355
    15ac0ae6-8ac2-4248-af69-756bb0abf534    COMPLETE    2023-04-20 00:31:28.319685
    fc95304a-b713-4468-a128-d5155c85333a    COMPLETE    2023-04-20 00:32:33.332482
    4a42a175-8065-4def-969a-b33ddc1bbdba    COMPLETE    2023-04-20 00:33:38.345533
    Restoration UUID                        State
    a61046a2-8b77-4d6e-87e1-1dc44b5ebc69    RESTORED
    2bc005ca-c842-4c7c-9cc7-34e1f75ca467    RESTORED
    ```

1. Verify that the table exists with the data:

    ```sh
    ./bin/ysqlsh -d yugabyte;
    ```

    ```sql
    select * from employees;
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

### Undo table alteration

#### Undo column addition

1. Verify that a snapshot has happened since table restoration:

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        list_snapshot_schedules
    ```

    ```output.json
    {
        "schedules": [
            {
                "id": "1fb2d85a-3608-4cb1-af63-3e4062300dc1",
                "options": {
                    "filter": "ysql.yugabyte",
                    "interval": "1 min",
                    "retention": "10 min"
                },
                "snapshots": [
                    {
                        "id": "e4c12e39-6b15-49f2-97d1-86f777650d6b",
                        "snapshot_time": "2023-04-20 00:26:03.257519",
                        "previous_snapshot_time": "2023-04-20 00:24:58.246932"
                    },
                    {
                        "id": "3d1176d0-f56d-44f3-bb29-2fcb9b08186b",
                        "snapshot_time": "2023-04-20 00:27:08.272905",
                        "previous_snapshot_time": "2023-04-20 00:26:03.257519"
                    },
                    [...]
                    {
                        "id": "d30fb638-6315-466a-a080-a6050e0dbb04",
                        "snapshot_time": "2023-04-20 00:34:43.358691",
                        "previous_snapshot_time": "2023-04-20 00:33:38.345533"
                    }
                ]
            }
        ]
    }
    ```

1. To restore from an absolute time, get a timestamp from the command prompt. You'll add a column to the table, then restore to this time in order to undo the column addition:

    ```sh
    python -c 'import datetime; print(datetime.datetime.now().strftime("%s%f"))'
    ```

    ```output
    1681965472490517
    ```

1. Using the same database, alter your table by adding a column:

    ```sql
    alter table employees add column v2 int;
    select * from employees;
    ```

    ```output
     employee_no |      name      | department | salary | v2
    -------------+----------------+------------+--------+----
            1223 | Lucille Ball   | Operations |  70000 |
            1224 | John Zimmerman | Sales      |  60000 |
            1221 | John Smith     | Marketing  |  50000 |
            1222 | Bette Davis    | Sales      |  55000 |
    (4 rows)
    ```

1. At a terminal prompt, restore the snapshot schedule to the timestamp you obtained before you added the column:

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        restore_snapshot_schedule 1fb2d85a-3608-4cb1-af63-3e4062300dc1 1681965472490517
    ```

    ```output.json
    {
        "snapshot_id": "b3c12c51-e7a3-41a5-bf0d-77cde8520527",
        "restoration_id": "470a8e0b-9fe4-418f-a13a-773bdedca013"
    }
    ```

1. Verify the restoration is in `RESTORED` state (you'll see more snapshots in the list, as well):

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        list_snapshots
    ```

    ```output
    Snapshot UUID                           State       Creation Time
    e1900004-9a89-4c3a-b60b-4b570058c4da    COMPLETE    2023-04-20 00:30:23.306355
    15ac0ae6-8ac2-4248-af69-756bb0abf534    COMPLETE    2023-04-20 00:31:28.319685
    fc95304a-b713-4468-a128-d5155c85333a    COMPLETE    2023-04-20 00:32:33.332482
    4a42a175-8065-4def-969a-b33ddc1bbdba    COMPLETE    2023-04-20 00:33:38.345533
    d30fb638-6315-466a-a080-a6050e0dbb04    COMPLETE    2023-04-20 00:34:43.358691
    d228210b-cd87-4a74-bff6-42108f73456f    COMPLETE    2023-04-20 00:35:48.372783
    390e4fec-8aa6-466d-827d-6bee435af5aa    COMPLETE    2023-04-20 00:36:53.394833
    b3c12c51-e7a3-41a5-bf0d-77cde8520527    COMPLETE    2023-04-20 00:37:58.408458
    d99317fe-6d20-4c7f-b469-ffb16409fbcf    COMPLETE    2023-04-20 00:39:03.419109
    Restoration UUID                        State
    a61046a2-8b77-4d6e-87e1-1dc44b5ebc69    RESTORED
    2bc005ca-c842-4c7c-9cc7-34e1f75ca467    RESTORED
    470a8e0b-9fe4-418f-a13a-773bdedca013    RESTORED
    ```

1. Check that the v2 column is gone:

    ```sql
    select * from employees;
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

#### Undo column deletion

1. To restore from an absolute time, get a timestamp from the command prompt. You'll remove a column from the table, then restore to this time to get the column back:

    ```sh
    python -c 'import datetime; print(datetime.datetime.now().strftime("%s%f"))'
    ```

    ```output
    1681965684502460
    ```

1. Using the same database, alter your table by dropping a column:

    ```sql
    alter table employees drop salary;
    select * from employees;
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

1. Restore the snapshot schedule to the timestamp you obtained before you dropped the column, at a terminal prompt.

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        restore_snapshot_schedule 1fb2d85a-3608-4cb1-af63-3e4062300dc1 1681965684502460
    ```

    ```output
    {
        "snapshot_id": "49311e65-cc5b-4d41-9f87-e84d630016a9",
        "restoration_id": "fe08826b-9b1d-4621-99ca-505d1d58e184"
    }
    ```

1. Verify the restoration is in `RESTORED` state (you'll see more snapshots in the list, as well):

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        list_snapshots
    ```

    ```output
    Snapshot UUID                           State       Creation Time
    4a42a175-8065-4def-969a-b33ddc1bbdba    COMPLETE    2023-04-20 00:33:38.345533
    d30fb638-6315-466a-a080-a6050e0dbb04    COMPLETE    2023-04-20 00:34:43.358691
    d228210b-cd87-4a74-bff6-42108f73456f    COMPLETE    2023-04-20 00:35:48.372783
    390e4fec-8aa6-466d-827d-6bee435af5aa    COMPLETE    2023-04-20 00:36:53.394833
    b3c12c51-e7a3-41a5-bf0d-77cde8520527    COMPLETE    2023-04-20 00:37:58.408458
    d99317fe-6d20-4c7f-b469-ffb16409fbcf    COMPLETE    2023-04-20 00:39:03.419109
    3f6651a5-00b2-4a9d-99e2-63b8b8e75ccf    COMPLETE    2023-04-20 00:40:08.432723
    7aa1054a-1c96-4d33-bd37-02cdefaa5cad    COMPLETE    2023-04-20 00:41:13.445282
    49311e65-cc5b-4d41-9f87-e84d630016a9    COMPLETE    2023-04-20 00:42:18.454674
    Restoration UUID                        State
    a61046a2-8b77-4d6e-87e1-1dc44b5ebc69    RESTORED
    2bc005ca-c842-4c7c-9cc7-34e1f75ca467    RESTORED
    470a8e0b-9fe4-418f-a13a-773bdedca013    RESTORED
    fe08826b-9b1d-4621-99ca-505d1d58e184    RESTORED
    ```

1. Verify that the salary column is back:

    ```sql
    select * from employees;
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

1. To restore from an absolute time, get a timestamp from the command prompt. You'll create an index on the table, then restore to this time to undo the index creation:

    ```sh
    python -c 'import datetime; print(datetime.datetime.now().strftime("%s%f"))'
    ```

    ```output
    1681965868912921
    ```

1. Create an index on the table:

    ```sql
    create index t1_index on employees (employee_no);
    \d employees;
    ```

    ```output
                  Table "public.employees"
       Column    |  Type   | Collation | Nullable | Default
    -------------+---------+-----------+----------+---------
     employee_no | integer |           | not null |
     name        | text    |           |          |
     department  | text    |           |          |
     salary      | integer |           |          |
     Indexes:
         "employees_pkey" PRIMARY KEY, lsm (employee_no HASH)
         "t1_index" lsm (employee_no HASH)
    ```

1. Restore the snapshot schedule to the timestamp you obtained before you created the index, at a terminal prompt:

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        restore_snapshot_schedule 1fb2d85a-3608-4cb1-af63-3e4062300dc1 1681965868912921
    ```

    ```output
    {
        "snapshot_id": "6a014fd7-5aad-4da0-883b-0c59a9261ed6",
        "restoration_id": "6698a1c4-58f4-48cb-8ec7-fa7b31ecca72"
    }
    ```

1. Verify the restoration is in `RESTORED` state (you'll see more snapshots in the list, as well):

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        list_snapshots
    ```

    ```output
    Snapshot UUID                           State       Creation Time
    390e4fec-8aa6-466d-827d-6bee435af5aa    COMPLETE    2023-04-20 00:36:53.394833
    b3c12c51-e7a3-41a5-bf0d-77cde8520527    COMPLETE    2023-04-20 00:37:58.408458
    d99317fe-6d20-4c7f-b469-ffb16409fbcf    COMPLETE    2023-04-20 00:39:03.419109
    3f6651a5-00b2-4a9d-99e2-63b8b8e75ccf    COMPLETE    2023-04-20 00:40:08.432723
    7aa1054a-1c96-4d33-bd37-02cdefaa5cad    COMPLETE    2023-04-20 00:41:13.445282
    49311e65-cc5b-4d41-9f87-e84d630016a9    COMPLETE    2023-04-20 00:42:18.454674
    c6d37ea5-002e-4dff-b691-94d458f4b1f9    COMPLETE    2023-04-20 00:43:23.469233
    98879e83-d507-496c-aa69-368fc2de8cf8    COMPLETE    2023-04-20 00:44:28.476244
    6a014fd7-5aad-4da0-883b-0c59a9261ed6    COMPLETE    2023-04-20 00:45:33.467234
    Restoration UUID                        State
    a61046a2-8b77-4d6e-87e1-1dc44b5ebc69    RESTORED
    2bc005ca-c842-4c7c-9cc7-34e1f75ca467    RESTORED
    470a8e0b-9fe4-418f-a13a-773bdedca013    RESTORED
    fe08826b-9b1d-4621-99ca-505d1d58e184    RESTORED
    6698a1c4-58f4-48cb-8ec7-fa7b31ecca72    RESTORED
    ```

1. Verify that the index is gone:

    ```sql
    \d employees;
    ```

    ```output
                Table "public.employees"
       Column    |  Type   | Collation | Nullable | Default
    -------------+---------+-----------+----------+---------
     employee_no | integer |           | not null |
     name        | text    |           |          |
     department  | text    |           |          |
     salary      | integer |           |          |
     Indexes:
         "employees_pkey" PRIMARY KEY, lsm (employee_no HASH)
    ```

Along similar lines, you can undo index deletions and alter table rename columns.

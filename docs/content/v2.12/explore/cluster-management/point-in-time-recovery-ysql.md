---
title: Point-in-Time Recovery for YSQL
headerTitle: Point-in-time recovery
linkTitle: Point-in-time recovery
description: Restore data from a specific point in time in YugabyteDB for YSQL
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  v2.12:
    identifier: cluster-management-point-in-time-recovery-ysql
    parent: explore-cluster-management
    weight: 704
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../point-in-time-recovery-ysql" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../point-in-time-recovery-ycql" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

The point-in-time recovery feature allows you to restore the state of your cluster's data (and [certain types](../../../manage/backup-restore/point-in-time-recovery/#limitations) of metadata) from a specific point in time. This can be relative, such as "three hours ago", or an absolute timestamp.

Refer to [Features](../../../manage/backup-restore/point-in-time-recovery/#features), [Use cases](../../../manage/backup-restore/point-in-time-recovery/#use-cases), and [Limitations](../../../manage/backup-restore/point-in-time-recovery/#limitations) for details on this feature. For more details on the `yb-admin` commands, refer to the [Backup and snapshot commands](../../../admin/yb-admin/#backup-and-snapshot-commands) section of the yb-admin documentation.

You can try out the PITR feature by creating a database and populating it, creating a snapshot schedule, and restoring (be sure to check out the [limitations](../../../manage/backup-restore/point-in-time-recovery/#limitations)!) from a snapshot on the schedule.

{{< tip title="Examples are simplified" >}}

The examples on this page are deliberately simplified. In many of the scenarios presented, you could drop the index or table to recover. Consider the examples as part of an effort to undo a larger schema change, such as a database migration, which has performed several operations.

{{< /tip >}}

## Undo data changes

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

1. At a terminal prompt, create a snapshot schedule for the database from a shell prompt. In this example, the schedule is one snapshot every minute, and each snapshot is retained for ten minutes.

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 create_snapshot_schedule 1 10 ysql.yugabyte
    ```

    ```output
    {
        "schedule_id": "0e4ceb83-fe3d-43da-83c3-013a8ef592ca"
    }
    ```

1. Verify that a snapshot has happened:

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 list_snapshot_schedules
    ```

    ```output
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

1. From a command prompt, get a timestamp.

    ```sh
    $ python -c 'import datetime; print(datetime.datetime.now().strftime("%s%f"))'
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

1. Restore the snapshot schedule to the timestamp you obtained before you added the data, at a terminal prompt.

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 restore_snapshot_schedule 0e4ceb83-fe3d-43da-83c3-013a8ef592ca 1620418817729963
    ```

    ```output
    {
        "snapshot_id": "2287921b-1cf9-4bbc-ad38-e309f86f72e9",
        "restoration_id": "1c5ef7c3-a33a-46b5-a64e-3fa0c72709eb"
    }
    ```

1. Next, verify the restoration is in `RESTORED` state (you'll see more snapshots in the list, as well):

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 list_snapshots
    ```

    ```output
    Snapshot UUID                         State
    8d588cb7-13f2-4bda-b584-e9be47a144c5  COMPLETE
    1f4db0e2-0706-45db-b157-e577702a648a  COMPLETE
    b91c734b-5c57-4276-851e-f982bee73322  COMPLETE
    04fc6f05-8775-4b43-afbd-7a11266da110  COMPLETE
    e7bc7b48-351b-4713-b46b-dd3c9c028a79  COMPLETE
    2287921b-1cf9-4bbc-ad38-e309f86f72e9  COMPLETE
    97aa2968-6b56-40ce-b2c5-87d2e54e9786  COMPLETE
    Restoration UUID                      State
    1c5ef7c3-a33a-46b5-a64e-3fa0c72709eb  RESTORED
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

### Undo table creation

1. At a terminal prompt, create a snapshot schedule for the database. In this example, the schedule is on the default `yugabyte` database, one snapshot every minute, and each snapshot is retained for ten minutes.

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 create_snapshot_schedule 1 10 ysql.yugabyte
    ```

    ```output
    {
      "schedule_id": "1ccb7e8b-4032-48b9-ac94-9f425d270a97"
    }
    ```

1. Verify that a snapshot has happened.

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 list_snapshot_schedules
    ```

    ```output
    {
      "schedules": [
          {
              "id": "1ccb7e8b-4032-48b9-ac94-9f425d270a97",
              "options": {
                  "filter": "ysql.yugabyte",
                  "interval": "1 min",
                  "retention": "10 min"
              },
              "snapshots": [
                  {
                      "id": "94052190-1f39-44f3-b66f-87e40e1eca04",
                      "snapshot_time": "2021-08-02 22:22:55.251562"
                  }
              ]
          }
      ]
    }
    ```

1. To restore from an absolute time, get a timestamp from the command prompt. You'll create a table, then restore to this time to undo the table creation.

    ```sh
    $ python -c 'import datetime; print(datetime.datetime.now().strftime("%s%f"))'
    ```

    ```output
    1627943076717734
    ```

1. Start the YSQL shell and connect to your local instance.

    ```sh
    $ bin/ysqlsh -h 127.0.0.1
    ```

1. Create a table and populate some sample data.

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

1. Restore the snapshot schedule to the timestamp you obtained before you created the table, at a terminal prompt.

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 restore_snapshot_schedule 1ccb7e8b-4032-48b9-ac94-9f425d270a97 1627943076717734
    ```

    ```output
    {
      "snapshot_id": "5911ba63-9bde-4170-917e-2ee06a686e12",
      "restoration_id": "e059741e-1cff-4cf7-99c5-3c351c0ce22b"
    }
    ```

1. Next, verify the restoration is in `RESTORED` state (you'll see more snapshots in the list, as well).

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 list_snapshots
    ```

    ```output
    Snapshot UUID                           State
    94052190-1f39-44f3-b66f-87e40e1eca04    COMPLETE
    d4e9879d-1873-4533-9a09-c0cd1aa34317    COMPLETE
    5911ba63-9bde-4170-917e-2ee06a686e12    COMPLETE
    05ae7198-a2a3-4374-b4ee-49ba38c8bc74    COMPLETE
    6926fdd4-7cec-408e-b247-511b504499c1    COMPLETE
    eec02516-c10a-4369-8ff5-ed1c7f129749    COMPLETE
    Restoration UUID                        State
    e059741e-1cff-4cf7-99c5-3c351c0ce22b    RESTORED
    ```

1. Verify that the table no longer exists.

    ```sh
    $ bin/ysqlsh -d yugabyte;
    ```

    ```sql
    \d employees;
    ```

    ```output
    Did not find any relation named "employees".
    ```

### Undo table deletion

1. At a terminal prompt, create a snapshot schedule for the database. In this example, the schedule is on the default `yugabyte` database, one snapshot every minute, and each snapshot is retained for ten minutes.

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 create_snapshot_schedule 1 10 ysql.yugabyte
    ```

    ```output
    {
      "schedule_id": "b4217ea5-56dc-4daf-afea-743460ece241"
    }
    ```

1. Start the YSQL shell and connect to your local instance.

    ```sh
    $ bin/ysqlsh -h 127.0.0.1
    ```

1. Create a table and populate some sample data.

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

1. Verify that a snapshot has happened since table creation.

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 list_snapshot_schedules
    ```

    ```output
      {
      "schedules": [
          {
              "id": "b4217ea5-56dc-4daf-afea-743460ece241",
              "options": {
                  "filter": "ysql.yugabyte",
                  "interval": "1 min",
                  "retention": "10 min"
              },
              "snapshots": [
                  {
                      "id": "2739695f-7b61-4996-98b3-c2a4052fd840",
                      "snapshot_time": "2021-08-03 11:24:28.632182"
                  },
                  {
                      "id": "0894192c-6326-4110-a5c3-fbdaaaac7d98",
                      "snapshot_time": "2021-08-03 11:25:33.641747",
                      "previous_snapshot_time": "2021-08-03 11:24:28.632182"
                  },
                  {
                      "id": "17364e01-e0e3-4ec5-a0e7-d69c45622351",
                      "snapshot_time": "2021-08-03 11:26:38.652024",
                      "previous_snapshot_time": "2021-08-03 11:25:33.641747"
                  }
              ]
          }
      ]
      }
    ```

1. To restore from an absolute time, get a timestamp from the command prompt. You'll delete the table, then restore to this time to undo the delete.

    ```sh
    $ python -c 'import datetime; print(datetime.datetime.now().strftime("%s%f"))'
    ```

    ```output
    1627990118725202
    ```

1. Drop this table.

    ```sql
    drop table employees;
    ```

1. Restore the snapshot schedule to the timestamp you obtained before you deleted the table, at a terminal prompt.

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 restore_snapshot_schedule b4217ea5-56dc-4daf-afea-743460ece241 1627990118725202
    ```

    ```output
    {
      "snapshot_id": "663cec5d-48e7-4f27-89ac-94c2dd0a3c32",
      "restoration_id": "eda28aa5-10bc-431d-ade9-44c9b8d1810e"
    }
    ```

1. Next, verify the restoration is in `RESTORED` state (you'll see more snapshots in the list, as well).

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 list_snapshots
    ```

    ```output
    Snapshot UUID                           State
    2739695f-7b61-4996-98b3-c2a4052fd840    COMPLETE
    0894192c-6326-4110-a5c3-fbdaaaac7d98    COMPLETE
    17364e01-e0e3-4ec5-a0e7-d69c45622351    COMPLETE
    feff81c1-3d28-4712-9066-8bd889bbf970    COMPLETE
    663cec5d-48e7-4f27-89ac-94c2dd0a3c32    COMPLETE
    5b1e14d7-7ec2-42bc-bd8b-12dd17e0b452    COMPLETE
    11113719-ee1e-4052-a170-1a784e9cce0c    COMPLETE
    Restoration UUID                        State
    eda28aa5-10bc-431d-ade9-44c9b8d1810e    RESTORED
    ```

1. Verify that the table exists with the data.

    ```sh
    $ bin/ysqlsh -d yugabyte;
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

1. At a terminal prompt, create a snapshot schedule for the database. In this example, the schedule is on the default `yugabyte` database, one snapshot every minute, and each snapshot is retained for ten minutes.

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 create_snapshot_schedule 1 10 ysql.yugabyte
    ```

    ```output
    {
      "schedule_id": "47fd40c3-1c2f-4e1b-b64b-6c2c9f698946"
    }
    ```

1. Start the YSQL shell and connect to your local instance.

    ```sh
    $ bin/ysqlsh -h 127.0.0.1
    ```

1. Create a table and populate some sample data.

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

1. Verify that a snapshot has happened since table creation.

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 list_snapshot_schedules
    ```

    ```output
    {
      "schedules": [
          {
              "id": "47fd40c3-1c2f-4e1b-b64b-6c2c9f698946",
              "options": {
                  "filter": "ysql.yugabyte",
                  "interval": "1 min",
                  "retention": "10 min"
              },
              "snapshots": [
                  {
                      "id": "ded348d6-a046-4778-8574-edb793739c37",
                      "snapshot_time": "2021-08-03 11:59:20.979156"
                  },
                  {
                      "id": "e2bfb948-8f24-4a0a-877f-a792ee9c969d",
                      "snapshot_time": "2021-08-03 12:00:25.988691",
                      "previous_snapshot_time": "2021-08-03 11:59:20.979156"
                  },
                  {
                      "id": "93a58196-d758-4bc8-93f7-72db8d334864",
                      "snapshot_time": "2021-08-03 12:01:30.999535",
                      "previous_snapshot_time": "2021-08-03 12:00:25.988691"
                  },
                  {
                      "id": "3f29453c-1159-4199-b2b5-558ebf14702f",
                      "snapshot_time": "2021-08-03 12:02:36.009326",
                      "previous_snapshot_time": "2021-08-03 12:01:30.999535"
                  }
              ]
          }
      ]
      }
    ```

1. To restore from an absolute time, get a timestamp from the command prompt. You'll add a column to the table, then restore to this time in order to undo the column addition.

    ```sh
    $ python -c 'import datetime; print(datetime.datetime.now().strftime("%s%f"))'
    ```

    ```output
    1627992256752809
    ```

1. Using the same database, alter your table by adding a column.

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

1. At a terminal prompt, restore the snapshot schedule to the timestamp you obtained before you added the column.

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 restore_snapshot_schedule 47fd40c3-1c2f-4e1b-b64b-6c2c9f698946 1627992256752809
    ```

    ```output
    {
      "snapshot_id": "db876700-d553-49e4-a0d1-4c88e52d8a78",
      "restoration_id": "c240d26c-cbeb-46eb-b9ea-0a3e6a734ecf"
    }
    ```

1. Next, verify the restoration is in `RESTORED` state (you'll see more snapshots in the list, as well):

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 list_snapshots
    ```

    ```output
    Snapshot UUID                           State
    e2bfb948-8f24-4a0a-877f-a792ee9c969d    COMPLETE
    93a58196-d758-4bc8-93f7-72db8d334864    COMPLETE
    3f29453c-1159-4199-b2b5-558ebf14702f    COMPLETE
    24081a33-b6a4-4a98-a725-08410f7fcb03    COMPLETE
    db876700-d553-49e4-a0d1-4c88e52d8a78    COMPLETE
    0751efd2-5af1-4859-b07a-06ea1a3310e6    COMPLETE
    f6f656e1-1df9-4c73-8613-2dce2ba36f51    COMPLETE
    a35c4f76-0df2-4a15-b32f-d1410aac4c55    COMPLETE
    f394ecc2-b0bd-4118-b5f5-67ee4c42ea1d    COMPLETE
    Restoration UUID                        State
    c240d26c-cbeb-46eb-b9ea-0a3e6a734ecf    RESTORED
    ```

1. Check that the v2 column is gone.

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

1. At a terminal prompt, create a snapshot schedule for the database. In this example, the schedule is on the default `yugabyte` database, one snapshot every minute, and each snapshot is retained for ten minutes.

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 create_snapshot_schedule 1 10 ysql.yugabyte
    ```

    ```output
    {
      "schedule_id": "064d1734-377c-4842-a95e-88ce68c93ca9"
    }
    ```

1. Start the YSQL shell and connect to your local instance.

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

1. Verify that a snapshot has happened since table creation.

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 list_snapshot_schedules
    ```

    ```output
    {
      "schedules": [
          {
              "id": "064d1734-377c-4842-a95e-88ce68c93ca9",
              "options": {
                  "filter": "ysql.yugabyte",
                  "interval": "1 min",
                  "retention": "10 min"
              },
              "snapshots": [
                  {
                      "id": "967843c0-58b6-4bd1-943a-5a75a6d2588d",
                      "snapshot_time": "2021-08-03 12:17:47.471817"
                  },
                  {
                      "id": "6b77a0a2-1ecc-4d5d-8e13-4bb7b57cca1e",
                      "snapshot_time": "2021-08-03 12:18:52.482001",
                      "previous_snapshot_time": "2021-08-03 12:17:47.471817"
                  },
                  {
                      "id": "99ab0e01-af59-4a6a-8870-9de6bd535f6d",
                      "snapshot_time": "2021-08-03 12:19:57.492521",
                      "previous_snapshot_time": "2021-08-03 12:18:52.482001"
                  }
              ]
          }
      ]
    }
    ```

1. To restore from an absolute time, get a timestamp from the command prompt. You'll remove a column from the table, then restore to this time to get the column back.

    ```sh
    $ python -c 'import datetime; print(datetime.datetime.now().strftime("%s%f"))'
    ```

    ```output
    1627993283589019
    ```

1. Using the same database, alter your table by dropping a column.

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
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 restore_snapshot_schedule 064d1734-377c-4842-a95e-88ce68c93ca9 1627993283589019
    ```

    ```output
    {
      "snapshot_id": "814982b2-2d86-425a-8ea1-29e356e5de1f",
      "restoration_id": "3601225d-21ff-45e8-bebc-ff84c058d290"
    }
    ```

1. Next, verify the restoration is in `RESTORED` state (you'll see more snapshots in the list, as well):

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 list_snapshots
    ```

    ```output
    Snapshot UUID                           State
    967843c0-58b6-4bd1-943a-5a75a6d2588d    COMPLETE
    6b77a0a2-1ecc-4d5d-8e13-4bb7b57cca1e    COMPLETE
    99ab0e01-af59-4a6a-8870-9de6bd535f6d    COMPLETE
    e798b0c5-607c-4662-8b70-beb7ef672ab6    COMPLETE
    814982b2-2d86-425a-8ea1-29e356e5de1f    COMPLETE
    980a86e3-88f3-4ed9-815f-6ff7923d2bff    COMPLETE
    Restoration UUID                        State
    3601225d-21ff-45e8-bebc-ff84c058d290    RESTORED
    ```

1. Verify that the salary column is back.

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

1. At a terminal prompt, create a snapshot schedule for the database. In this example, the schedule is on the default `yugabyte` database, one snapshot every minute, and each snapshot is retained for ten minutes.

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 create_snapshot_schedule 1 10 ysql.yugabyte
    ```

    ```output
    {
      "schedule_id": "dcbe46e3-8108-4d50-8601-423b27d230b1"
    }
    ```

1. Start the YSQL shell and connect to your local instance.

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

1. Verify that a snapshot has happened since table creation.

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 list_snapshot_schedules
    ```

    ```output
    {
      "schedules": [
          {
              "id": "dcbe46e3-8108-4d50-8601-423b27d230b1",
              "options": {
                  "filter": "ysql.yugabyte",
                  "interval": "1 min",
                  "retention": "10 min"
              },
              "snapshots": [
                  {
                      "id": "b4316e4e-a7c2-49a3-af16-d928aef5630e",
                      "snapshot_time": "2021-08-03 12:36:56.212541"
                  },
                  {
                      "id": "c318b620-490c-4294-b495-a7f0349017d0",
                      "snapshot_time": "2021-08-03 12:38:01.221749",
                      "previous_snapshot_time": "2021-08-03 12:36:56.212541"
                  },
                  {
                      "id": "85dc006f-ebf5-464e-81f7-a406a3322942",
                      "snapshot_time": "2021-08-03 12:39:06.232231",
                      "previous_snapshot_time": "2021-08-03 12:38:01.221749"
                  }
              ]
          }
      ]
    }
    ```

1. To restore from an absolute time, get a timestamp from the command prompt. You'll create an index on the table, then restore to this time to undo the index creation.

    ```sh
    $ python -c 'import datetime; print(datetime.datetime.now().strftime("%s%f"))'
    ```

    ```output
    1627994453375139
    ```

1. Create an index on the table.

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

1. Restore the snapshot schedule to the timestamp you obtained before you created the index, at a terminal prompt.

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 restore_snapshot_schedule dcbe46e3-8108-4d50-8601-423b27d230b1 1627994453375139
    ```

    ```output
    {
      "snapshot_id": "d57114c1-c8cd-42b2-83b2-66960112d5c9",
      "restoration_id": "f7943fe6-d6fb-45e1-9086-2de864543d62"
    }
    ```

1. Next, verify the restoration is in `RESTORED` state (you'll see more snapshots in the list, as well):

    ```sh
    $ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 list_snapshots
    ```

    ```output
    Snapshot UUID                           State
    b4316e4e-a7c2-49a3-af16-d928aef5630e    COMPLETE
    c318b620-490c-4294-b495-a7f0349017d0    COMPLETE
    85dc006f-ebf5-464e-81f7-a406a3322942    COMPLETE
    d48af5a1-0f59-4e43-a24f-f40fa4e278d6    COMPLETE
    d57114c1-c8cd-42b2-83b2-66960112d5c9    COMPLETE
    5b1a1eae-3205-412f-a998-7db604900cdb    COMPLETE
    87a47c86-c3bc-4871-996a-c288bb2b5c4f    COMPLETE
    1114026c-8504-4f37-8179-4798ff6008e2    COMPLETE
    Restoration UUID                        State
    f7943fe6-d6fb-45e1-9086-2de864543d62    RESTORED
    ```

1. Verify that the index is gone.

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

{{< tip title="Other metadata changes" >}}

Along similar lines, you can also undo index deletions and alter table rename columns.

{{< /tip >}}

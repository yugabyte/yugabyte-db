---
title: Database clone
headerTitle: Database clone
linkTitle: Database clone
description: Clone your database in YugabyteDB for data recovery, development, and testing.
badges: tp
menu:
  preview:
    identifier: lightweight-db-clone
    parent: backup-restore
    weight: 706
type: docs
---

Database cloning in YugabyteDB allows you to quickly create an independent copy of your database for data recovery, development, and testing. The process is efficient because the clone starts by using the same data files as the original database. After the initial creation, the cloned database serves reads and writes independently, creates and maintains its own changes as delta files, separately from the original database.

The two databases are completely isolated, meaning you can experiment with the cloned database, perform DDL operations, read and write data, and even delete the clone without impacting the original database.

Additionally, you can create a clone of the database from a specific point in time within a configurable retention period, making it particularly useful in data recovery scenarios due to user or application error. For example, if you accidentally dropped a table at 9:01, you can create a clone from 9:00 (just before the table drop) to restore the lost data to the production database.

## Use cases

### Data recovery

If you encounter data loss due to user error (for example, accidentally dropping a table) or application error (for example, updating rows with corrupted data), you can quickly create a clone of your production database from a point in time when database was in a good state. This allows you to perform forensic analysis and restore the lost or corrupted data back to the original database.

### Development and testing

You can instantly create an isolated copy of your production database for development and testing purposes. This copy includes the latest data from the production database, enabling developers to test their changes on an identical copy of the production database without affecting the performance of the production database.

## Enable database cloning

To enable database cloning in a cluster, you set the YB-Master flag `enable_db_clone` to true. Additionally, as cloning is in {{<badge/tp>}}, the flag `enable_db_clone` should be added to the `allowed_preview_flags_csv`list.

**Using yugabyted**: You can set these two flags using the `--master_flags` option of [yugabyted](../../../../reference/configuration/yugabyted/) as follows:

```sh
./bin/yugabyted start --master_flags "allowed_preview_flags_csv={enable_db_clone},enable_db_clone=true"
```

**Using yb-ts-cli**: You can set the runtime flags while the yb-master process is running using [set_flag](../../../admin/yb-ts-cli/#set-flag) option of [yb-ts-cli](../../../admin/yb-ts-cli/) as follows:

```sh
./bin/yb-ts-cli --server-address=master_host:7100 set_flag allowed_preview_flags_csv enable_db_clone
./build/latest/bin/yb-ts-cli --server-address=127.0.0.1:7100 set_flag enable_db_clone true
```

## How to use database cloning

### Prerequisites

- [Create a snapshot schedule](../../../manage/backup-restore/point-in-time-recovery/#create-a-schedule) for the database you want to clone.

This keeps the history of the updates to a certain retention period. After that you can create a clone of the original database as of any point in time within the specified retention period.

- You have to trust local YSQL connections (that use UNIX domain sockets) in the [host based authentication](../../../secure/authentication/host-based-authentication/). You have to do this for all the yb-tservers in the cluster. You can do this when starting the yb-tserver process by adding the authentication line `local all all trust` to the yb-tserver flag, [ysql_hba_conf_csv](../../../reference/configuration/yb-tserver/#ysql-hba-conf-csv).

For example, if you are using yb-ctl you can run the following command:

```sh
./bin/yb-ctl create --master_flags "enable_db_clone=true,allowed_preview_flags_csv=enable_db_clone" --tserver_flags '"ysql_hba_conf_csv=""host all all 0.0.0.0/0 trust,local all all trust"""'
```

{{<note title="Note">}}
Ensure to not override your default host based authentication rules when trusting the local connection. You may need to add additional authentication lines to `ysql_hba_conf_csv` based on your specific configuration. For more information, see [host based authentication](../../../secure/authentication/host-based-authentication/).
{{</note>}}

### Cloning in YSQL

Because YugabyteDB is PostgreSQL compatible, you can create a database as a clone of another using the `TEMPLATE` SQL option of `CREATE DATABASE` command as follows:

```sql
CREATE DATABASE clone_db TEMPLATE original_db;
```

`clone_db` is created as a clone of database `original_db` which contains the latest schema and data of `original_db` as of current time.
Additionally, to create a clone of the original database at a specific point in time in the past (within the history retention period specified when creating the snapshot schedule) you can specify the [Unix timestamp](https://www.unixtimestamp.com/) in microseconds in the `AS OF` option as follows:

```sql
CREATE DATABASE clone_db TEMPLATE origginal_db AS OF 1723146703674480;
```

### Cloning in YCQL

You can create a clone in YCQL using the yb-admin command as follows:

```sh
./bin/yb-admin --master_addresses $MASTERS clone_namespace ycql.originaldb1 clonedb2 1715275616599020
```

`clonedb2` is created as as a clone of the ycql database `originaldb1` as of 1715275616599020 Unix timestamp.

### Check the clone status

You can check the status of clone operations performed on a database with the `source_database_id` (YSQL)/`source_namespace_id`(YCQL) field using the yb-admin command `list_clones` as follows:

```sh
./bin/yb-admin --master_addresses $MASTERS list_clones 00004000000030008000000000000000
```

```output
[
    {
        "aggregate_state": "COMPLETE",
        "source_namespace_id": "00004000000030008000000000000000",
        "seq_no": "1",
        "target_namespace_name": "testing_clone_db",
        "restore_time": "2024-08-09 21:42:16.451974"
    },
    {
        "aggregate_state": "COMPLETE",
        "source_namespace_id": "00004000000030008000000000000000",
        "seq_no": "2",
        "target_namespace_name": "dev_clone_db",
        "restore_time": "2024-08-09 21:42:55.048663"
    }
]
```

Note that there were two clone operations performed on the source database `00004000000030008000000000000000` that are COMPLETE. The two clones are `testing_clone_db` and `dev_clone_db` and they each have a unique `seq_no` used to identify each clone operation from the same source database.

Alternatively, you can check the status of a specific clone operation if you have both the `source_database_id` (YSQL)/`source_namespace_id`(YCQL) and the `seq_no` as follows:

```sh
./bin/yb-admin --master_addresses $MASTERS list_clones 00004000000030008000000000000000 2
```

```output
[
    {
        "aggregate_state": "COMPLETE",
        "source_namespace_id": "00004000000030008000000000000000",
        "seq_no": "1",
        "target_namespace_name": "testing_clone_db",
        "restore_time": "2024-08-09 21:42:16.451974"
    },
    {
        "aggregate_state": "COMPLETE",
        "source_namespace_id": "00004000000030008000000000000000",
        "seq_no": "2",
        "target_namespace_name": "dev_clone_db",
        "restore_time": "2024-08-09 21:42:55.048663"
    }
]
```

This command can be helpful to check whether a clone operation completed successfully or not.

Note that the cluster doesn't allow two clone operations to happen concurrently on the same source database.

### Example

{{% explore-setup-single %}}

The following example uses [ysqlsh](../../../admin/ysqlsh/) to create a database clone for recovering from an accidental table deletion by a user.

1. Start ysqlsh and create the database as follows:

    ```sh
    ./bin/ysqlsh
    CREATE DATABASE production_db;
    ```

1. Create a snapshot schedule that produces a snapshot once a day (every 1,440 minutes), and retains it for three days (4,320 minutes) using the following command:

    ```sh
    ./bin/yb-admin --master_addresses ip1:7100,ip2:7100,ip3:7100 create_snapshot_schedule 1440 4320 ysql.production_db
    ```

1. Create a table `t1` and add some data as follows:

    ```sql
    \c production_db;
    production_db=# CREATE TABLE t1 (k INT, v INT);
    production_db=# INSERT INTO t1 (k,v) SELECT i,i%2 FROM generate_series(1,5) AS i;
    production_db=# SELECT * FROM t1 ORDER BY k;
    ```

    ```output
    k | v
   ---+---
    1 | 1
    2 | 0
    3 | 1
    4 | 0
    5 | 1
   (5 rows)
    ```

1. Determine the exact time when your database is in the correct state. This timestamp will be used to create a clone of the production database from the point when it was in the desired state. Execute the following SQL query to retrieve the current time in UNIX timestamp format:

    ```sql
    production_db=# SELECT (EXTRACT (EPOCH FROM CURRENT_TIMESTAMP)*1000000)::decimal(38,0);
    ```

    ```output
         numeric
     ------------------
      1723243720285350
      (1 row)
    ```

1. Drop the table `t1` simulating a user error dropping the table accidentally.

    ```sql
    production_db=# DROP TABLE t1;
    ```

    ```output
    DROP TABLE
    ```

    ```sql
    production_db=# SELECT * FROM t1 ORDER BY k;
    ```

    ```output
    ERROR:  relation "t1" does not exist
    LINE 1: SELECT * FROM t1 ORDER BY k;
    ```

   The table is dropped and there is no way you can query it.

1. Create a database `clone_db` using `production_db` as the clone template and the clone happens as of the time marked at step 4.

    ```sql
    production_db=# CREATE DATABASE clone_db TEMPLATE production_DB AS OF 1723243720285350;
    ```

    ```sql
    \c clone_db
    ```

    ```output
    You are now connected to database "clone_db" as user "yugabyte".
    ```

    ```sql
    clone_db=# SELECT * FROM t1 ORDER BY k;
    ```

    ```output
     k | v
    ---+---
     1 | 1
     2 | 0
     3 | 1
     4 | 0
     5 | 1
    (5 rows)
    ```

    `clone_db` is created and it contains all the data of `production_db` as of time at step 4 which means you can read table `t1` that was dropped accidentally. You can copy the data back to `production_db` or switch the workload to `clone_db`. You now have two isolated databases that can serve read and write independently.

## Best practices

Although creating a clone database is quick and doesn't have any space amplification as there is no physical data copying, it's important to understand that a clone creates an independent set of logical tablets. This effectively doubles the number of tablets, although the clone database's tablets share the same data files as the original database. Keep in mind the following impacts:

- Higher CPU usage due to the additional tablets
- Increased memory consumption from the extra tablets
- Potentially up to twice the disk usage (after major compactions) due to the retention of older data versions

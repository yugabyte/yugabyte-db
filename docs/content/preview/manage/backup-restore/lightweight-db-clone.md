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

Instant database cloning in YugabyteDB allows you to quickly create a zero-copy, independent writable clone of your database that can be used for data recovery, development, and testing. Cloning is both fast and efficient because when initially created, it shares the same data files with the original database. Subsequently, as data is written to the clone, the clone stores its own changes as separate and independent delta files. Although they physically share some files, the two databases are logically isolated, which means you can freely play with the clone database, perform DDLs, read and write data, and delete it without affecting the original database.

You can create clones as of now, or as of any time in the recent past, within a configurable history retention period. This is particularly useful for data recovery due to user or application error scenarios. For instance, if you dropped a table by mistake at 9:01, then detected this error at 10.45, you are interested in getting the lost data as it was at 9:00 (just before the drop table). At the same time, you don't want to lose any new data added to other tables between 9:01 and 10:45. With databse cloning, you can create a clone of the database at 9:00 (before the table drop) and restore the lost data back to the production database.

![Database clone](/images/manage/backup-restore/db-clone.png)

Cloning has two main use cases:

- Development and testing. Because the two databases are completely isolated, you can experiment with the cloned database, perform DDL operations, read and write data, and delete the clone without impacting the original. Developers can test their changes on an identical copy of the production database without affecting its performance.

- Data recovery. To recover from data loss due to user error (for example, accidentally dropping a table) or application error (for example, updating rows with corrupted data), you can create a clone of your production database from a point in time when the database was in a good state. This allows you to perform forensic analysis, export the lost or corrupted data from the clone, and import it back to the original database.

## Enable database cloning

To enable database cloning in a cluster, you set the YB-Master flag `enable_db_clone` to true. Because cloning is in {{<badge/tp>}}, you must also add the `enable_db_clone` flag to the [allowed_preview_flags_csv](../../../reference/configuration/yb-master/#allowed-preview-flags-csv) list.

For example, to set these flags when creating a cluster using yugabyted, use the `--master_flags` option of the [start](../../../reference/configuration/yugabyted/#start) command as follows:

```sh
./bin/yugabyted start advertise_address=127.0.0.1 \
    --master_flags "allowed_preview_flags_csv={enable_db_clone},enable_db_clone=true" \
    --tserver_flags "ysql_hba_conf_csv={host all all 0.0.0.0/0 trust,local all all trust}"
```

You can set the runtime flags while the yb-master process is running using the yb-ts-cli [set_flag](../../../admin/yb-ts-cli/#set-flag) command as follows:

```sh
./bin/yb-ts-cli --server-address=master_host:7100 set_flag allowed_preview_flags_csv enable_db_clone
./bin/yb-ts-cli --server-address=127.0.0.1:7100 set_flag enable_db_clone true
```

## Clone databases

### Prerequisites

- [Create a snapshot schedule](../../../manage/backup-restore/point-in-time-recovery/#create-a-schedule) for the database you want to clone.

    For example, creating a snapshot schedule with retention period of 7 days keeps the history of the updates for the entire 7 days. After that you can create a clone of the original database as of any point in time within the specified retention period.

- You have to trust local YSQL connections (that use UNIX domain sockets) in the [host-based authentication](../../../secure/authentication/host-based-authentication/). You have to do this for all YB-TServers in the cluster. You can do this when starting the YB-TServer process by adding the authentication line `local all all trust` to the [ysql_hba_conf_csv](../../../reference/configuration/yb-tserver/#ysql-hba-conf-csv) flag.

    For example, if you are using yugabyted you can run the following command:

    ```sh
   ./bin/yugabyted start --advertise_address=127.0.0.1 \
       --master_flags "allowed_preview_flags_csv={enable_db_clone},enable_db_clone=true" \
       --tserver_flags "ysql_hba_conf_csv={host all all 0.0.0.0/0 trust,local all all trust}"
    ```

{{<note title="Note">}}
Do not override your default host-based authentication rules when trusting the local connection. You may need to add additional authentication lines to `ysql_hba_conf_csv` based on your specific configuration. For more information, see [host based authentication](../../../secure/authentication/host-based-authentication/).
{{</note>}}

### Clone a YSQL database

Because YugabyteDB is PostgreSQL compatible, you can create a database as a clone of another using the `TEMPLATE` SQL option of `CREATE DATABASE` command as follows:

```sql
CREATE DATABASE clone_db TEMPLATE original_db;
```

In this example, `clone_db` is created as a clone of `original_db`, and contains the latest schema and data of `original_db` as of current time.

To create a clone of the original database at a specific point in time (within the history retention period specified when creating the snapshot schedule), you can specify the [Unix timestamp](https://www.unixtimestamp.com/) in microseconds using the `AS OF` option as follows:

```sql
CREATE DATABASE clone_db TEMPLATE original_db AS OF 1723146703674480;
```

### Clone a YCQL keyspace

You can create a clone in YCQL using the yb-admin `clone_namespace` command as follows:

```sh
./bin/yb-admin --master_addresses $MASTERS clone_namespace ycql.originaldb1 clonedb2 1715275616599020
```

In this example, `clonedb2` is created as a clone of `originaldb1` as of 1715275616599020 Unix timestamp.

### Check the clone status

To check the status of clone operations performed on a database, use the yb-admin `list_clones` command and provide the `source_database_id` (YSQL) or `source_namespace_id` (YCQL), as follows:

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

You can find the `source_database_id` or `source_namespace_id` from the [YB-Master leader UI](../../../reference/configuration/default-ports/#servers) under the `/namespaces` end point.

In this example, two clones were made of the source database `00004000000030008000000000000000` that are COMPLETE. The two clones are `testing_clone_db` and `dev_clone_db` and they each have a unique `seq_no` used to identify each clone operation from the same source database.

You can check the status of a specific clone operation if you have both the `source_database_id` (YSQL) or `source_namespace_id`(YCQL) and the `seq_no` as follows:

```sh
./bin/yb-admin --master_addresses $MASTERS list_clones 00004000000030008000000000000000 2
```

```output
[
    {
        "aggregate_state": "COMPLETE",
        "source_namespace_id": "00004000000030008000000000000000",
        "seq_no": "2",
        "target_namespace_name": "dev_clone_db",
        "restore_time": "2024-08-09 21:42:55.048663"
    }
]
```

Use the `list_clones` command to check whether a clone operation completed successfully or not.

Note that the cluster doesn't allow you to perform two clone operations concurrently on the same source database. You have to wait for the first clone to finish until you can perform another clone. However, you can create as many clones as you want for the same database or different databases.

### Example

The following example uses [ysqlsh](../../../admin/ysqlsh/) to create a database clone to recover from an accidental table deletion.

1. Create a local cluster using [yugabyted](../../../reference/configuration/yugabyted/) with the following configuration flags:

    ```sh
    ./bin/yugabyted start --advertise_address=127.0.0.1 \
        --master_flags "allowed_preview_flags_csv={enable_db_clone},enable_db_clone=true" \
        --tserver_flags "ysql_hba_conf_csv={host all all 0.0.0.0/0 trust,local all all trust}"
    ```

1. Start ysqlsh and create the database as follows:

    ```sh
    ./bin/ysqlsh
    CREATE DATABASE production_db;
    ```

1. Create a snapshot schedule that produces a snapshot once a day (every 1,440 minutes), and retains it for three days (4,320 minutes) using the following command:

    ```sh
    ./bin/yb-admin --master_addresses ip1:7100,ip2:7100,ip3:7100 create_snapshot_schedule 1440 4320 ysql.production_db
    ```

1. Create a two tables `t1` and `t2`, and add some data as follows:

    ```sql
    ./bin/ysqlsh
    yugabyte=# \c production_db;
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

    ```sql
    production_db=# CREATE TABLE t2 (key INT, c1 TEXT);
    production_db=# INSERT INTO t2 (key,c1) SELECT i,md5(random()::text) FROM generate_series(1,5) AS i;
    production_db=# SELECT * FROM t2 ORDER BY key;
    ```

    ```output
     key |                c1
    -----+----------------------------------
       1 | 450e6c49f86c76d944375e29e48f2dee
       2 | b934a3bdf7438458a85b0858c41f731c
       3 | 08697ed89ec387e714c6587e522d7a7e
       4 | a879ff99872b3c3433803d3c3229f0cf
       5 | 4d46a53780a7a348179e1af9b692e95e
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

1. Meanwhile, as table `t2` is still accepting reads/writes, you can insert 2 more rows as follows:

    ```sh
    INSERT INTO t2 (key,c1) SELECT i,md5(random()::text) FROM generate_series(6,7) AS i;
    ```

1. Now, if you try to query table `t1`, notice that the table is dropped and there is no way you can query it.

    ```sql
    production_db=# SELECT * FROM t1 ORDER BY k;
    ```

    ```output
    ERROR:  relation "t1" does not exist
    LINE 1: SELECT * FROM t1 ORDER BY k;
    ```

1. To get back lost data, create a database `clone_db` using `production_db` as the template and using the timestamp generated in step 4.

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

    `clone_db` contains all the data from `production_db` at the specified timestamp, which means you can read table `t1` that was dropped accidentally. You can copy the lost data from table `t1` back to `production_db` by exporting the data from the clone and importing it into `production_db`. Alternatively, you can switch the workload to `clone_db`. You now have two isolated databases that can serve reads and writes independently.

## Best practices

Although creating a clone database is quick and initially doesn't take up much added disk space as no data is copied, it's important to understand that a clone does create an independent set of logical tablets. This effectively doubles the number of tablets, although the clone database tablets share the same data files as the original database. Keep in mind the following impacts:

- Higher CPU usage due to the additional tablets
- Increased memory consumption from the extra tablets
- Increased space usage only if compaction is performed on clone or original database. This is because both original and post-compaction data files are kept in disk, that is, say compaction is performed on the source database which generates new compacted files. The old data files are kept to serve reads for the clone database.

## Limitations

- Cloning is not currently supported for databases that use sequences. See GitHub issue [21467](https://github.com/yugabyte/yugabyte-db/issues/21467) for tracking.
- Cloning to a time before dropping Materialized views is not currently supported. See GitHub issue [23740](https://github.com/yugabyte/yugabyte-db/issues/23740) for tracking.

---
title: View COPY status with pg_stat_progress_copy
linkTitle: Data transfer status
description: Use pg_stat_progress_copy to get the COPY command status, number of tuples processed, and other COPY progress reports.
headerTitle: View COPY status with pg_stat_progress_copy
menu:
  v2.20:
    identifier: pg-stat-progress-copy
    parent: explore-observability
    weight: 400
type: docs
---

YugabyteDB supports the PostgreSQL `pg_stat_progress_copy` view to report the progress of the COPY command execution. Whenever [COPY](../../../api/ysql/the-sql-language/statements/cmd_copy/) is running, the `pg_stat_progress_copy` view contains one row for each client connection that is currently running a COPY command.

The following table describes the view columns:

| Column | Description |
| :---- | :---------- |
| pid | Process ID of backend. |
| datid | Object ID of the database to which this backend is connected. |
| datname | Name of the database to which this backend is connected. |
| relid | Object ID of the table on which the COPY command is executed. It is set to 0 if copying from a SELECT query.|
| command | The command that is running COPY FROM, or COPY TO. |
| type | The I/O type that the data is read from or written to: FILE, PROGRAM, PIPE (for COPY FROM STDIN and COPY TO STDOUT), or CALLBACK (used for example during the initial table synchronization in logical replication). |
| yb_status | Tracking status of the copy command. |
| bytes_processed | Number of bytes already processed by the COPY command. |
| bytes_total | Size of the source file for the COPY FROM command in bytes. Set to 0 if not available. |
| tuples_processed | Number of tuples already processed by the COPY command. |
| tuples_excluded | Number of tuples not processed because they were excluded by the WHERE clause of the COPY command. |

## YugabyteDB-specific changes

The `pg_stat_progress_copy` view includes the following YugabyteDB-specific changes:

### Definition of `tuples_processed`

The definition of `tuples_processed` column is different in YugabyteDB in comparison to PostgreSQL.

In YugabyteDB, the `ROWS_PER_TRANSACTION` option is added to the COPY command, defining the transaction size to be used. For example, if the total tuples to be copied is 5000 and `ROWS_PER_TRANSACTION` is set to 1000, the database creates 5 transactions and each transaction inserts 1000 rows. If there is an error during execution, then some tuples can be persisted based on the already completed transaction.

Because each COPY is divided into multiple transactions, `tuples_processed` tracks the rows that the transaction has already completed.

For more information, refer to [ROWS_PER_TRANSACTION](../../../api/ysql/the-sql-language/statements/cmd_copy/#rows-per-transaction).

### New column `yb_status`

In YugabyteDB, the `pg_stat_progress_copy` view includes the column `yb_status` to indicate the status of the COPY command.

If a COPY command terminates due to any error, then it's possible that some tuples are persisted as explained in the [`tuples_processed`](#definition-of-tuples-processed) section. In this case, `tuples_processed` shows a non-zero count and `yb_status` shows that the COPY command was terminated due to an error. This is helpful for discovering whether the copy completed or not. This is unnecessary in PostgreSQL, where copy is a single transaction.

`yb_status` can have the following values:

- IN PROGRESS – Indicating that the COPY command is still running.
- ERROR – COPY command was terminated due to an error.
- SUCCESS – COPY command successfully completed.

### COPY command information

The `pg_stat_progress_copy` view retains copy command information after the copy operation has completed.

In PostgreSQL, when the COPY command finishes, the row containing details of the copy command is _removed_ from the view. In YugabyteDB, the information is _retained_ in the view after the copy has finished. The information is retained only for the last executed copy command on that connection.

This is required for YugabyteDB, because if the COPY command finishes due to an error, then you would like to know how many rows were reliably persisted to the disk.

## Examples

The following examples demonstrate the possible stages (IN PROGRESS, ERROR, SUCCESS) for the copy operation.

### Prerequisites

{{% explore-setup-single-local %}}

- Run the following script from your YugabyteDB installation directory to generate data to a file `test_data.csv`.

    ```sh
    #!/bin/bash
    rm -rf test_data.csv
    for i in {1..100000}
    do
       echo "$i, $i, $i" >> test_data.csv
    done
    ```

- From your local YugabyteDB installation directory, connect to the [YSQL](../../../admin/ysqlsh/) shell, and create a table using the following schema:

    ```sql
    create table test_copy ( h1 int, r1 int, v1 int, primary key (h1, r1));
    ```

### Run a successful COPY command

1. Copy `test_data.csv` using the following copy command:

    ```sql
    \copy test_copy from 'test_data.csv'  (DELIMITER ',');
    ```

    ```output
    COPY 100000
    ```

1. Verify `yb_status` using the view.

    ```sql
    select * from pg_stat_progress_copy ;
    ```

    You should see output similar to the following:

    ```output
     pid  | datid | datname  | relid |  command  | type | yb_status | bytes_processed | bytes_total | tuples_processed | tuples_excluded
     -----+-------+----------+-------+-----------+------+-----------+-----------------+-------------+------------------+-----------------
    74390 | 13288 | yugabyte | 16394 | COPY FROM | PIPE | SUCCESS   |         1966685 |           0 |           100000 |               0
    (1 row)
    ```

### Verify a COPY command status in progress

Copy `test_data.csv` using the following copy command and check the status in parallel from another terminal.

```sql
\copy test_copy from 'test_data.csv'  (DELIMITER ',');
```

```sql
select * from pg_stat_progress_copy ;
```

You should see output similar to the following:

```output
 pid  | datid | datname  | relid |  command  | type |  yb_status  | bytes_processed | bytes_total | tuples_processed | tuples_excluded
------+-------+----------+-------+-----------+------+-------------+-----------------+-------------+------------------+-----------------
74390 | 13288 | yugabyte | 16404 | COPY FROM | PIPE | IN PROGRESS |          766682 |           0 |            40000 |               0
(1 row)
```

### Verify an interrupted COPY operation

1. Copy `test_data.csv` using the following command, and then interrupt it using Ctrl+C:

    ```sql
    \copy test_copy from 'test_data.csv'  (DELIMITER ',');
    ```

    ```output
    ^CCancel request sent
    ERROR:  canceling statement due to user request
    CONTEXT:  COPY test_copy, line 55184: "55184, 55184, 55184"
    ````

1. Verify `yb_status` using the view.

     ```sql
    select * from pg_stat_progress_copy ;
    ```

    ```output
      pid  | datid | datname  | relid |  command  | type | yb_status | bytes_processed | bytes_total | tuples_processed | tuples_excluded
    -------+-------+----------+-------+-----------+------+-----------+-----------------+-------------+------------------+-----------------
     74390 | 13288 | yugabyte | 16399 | COPY FROM | PIPE | ERROR     |          766682 |           0 |            40000 |               0
    (1 row)
    ```

    The default for `ROWS_PER_TRANSACTION` is 20K and the interruption occurred on row 55184 in the example. As a result, the first two transactions of 20K rows were persisted resulting in 40K rows reported by `tuples_processed`. The next _in-progress_ transaction was interrupted, and for this reason the remaining 15184 rows were not persisted.

## Learn more

- Refer to [Get query statistics using pg_stat_statements](../../query-1-performance/pg-stat-statements/) to track planning and execution of all the SQL statements.
- Refer to [View live queries with pg_stat_activity](../pg-stat-activity/) to analyze live queries.
- Refer to [Analyze queries with EXPLAIN](../../query-1-performance/explain-analyze/) to optimize YSQL's EXPLAIN and EXPLAIN ANALYZE queries.
- Refer to [Optimize YSQL queries using pg_hint_plan](../../query-1-performance/pg-hint-plan/) show the query execution plan generated by YSQL.

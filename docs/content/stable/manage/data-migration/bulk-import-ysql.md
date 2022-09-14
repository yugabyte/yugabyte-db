---
title: Bulk import
headerTitle: Bulk import for YSQL
linkTitle: Bulk import
description: Import data from PostgreSQL into YugabyteDB for YSQL.
menu:
  stable:
    identifier: manage-bulk-import-ysql
    parent: manage-bulk-import-export
    weight: 706
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../bulk-import-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../bulk-import-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

This page describes the following steps to manually migrate PostgreSQL data and applications to YugabyteDB after [exporting PostgreSQL data](../../data-migration/bulk-export-ysql/).

- [Prepare a cluster](#prepare-a-cluster)
- [Import PostgreSQL data](#import-postgresql-data)
- [Verify a migration](#verify-a-migration)

## Prepare a cluster

This section outlines some of the important considerations before loading data into the cluster.

### Separate DDL schema from data

It is recommended to run the DDL schema generation first before loading the data exports. This is essential to ensure that the tables are properly created ahead of starting to use them.

### Order data by primary key

The data should be ordered by the primary key when it is being imported if possible. Importing a data set that is ordered by the primary key is typically much faster because the data being loaded will all get written to a node as a larger batch of rows, as opposed to writing a few rows across multiple nodes.

### Multiple parallel imports

It is more efficient if the source data being imported is split into multiple files, so that these files can be imported in parallel across the nodes of the cluster. This can be done by running multiple `COPY` commands in parallel. For example, a large CSV data file should be split into multiple smaller CSV files.

### Programmatic batch inserts

Following are some recommendations when performing batch loads of data programmatically.

1. Use multi-row inserts to do the batching. This would require setting certain properties based on the driver being used. For example, with the JDBC driver with Java, set the following property to use multi-row inserts: reWriteBatchedInserts=true

1. Use a batch size of 128 when using multi-row batch inserts.

1. Use the `PREPARE` - `BIND` - `EXECUTE` paradigm instead of inlining literals to avoid statement reparsing overhead.

1. To ensure optimal utilization of all nodes across the cluster by balancing the load, uniformly distribute the SQL statements across all nodes in the cluster.

1. It may be necessary to increase the parallelism of the load in certain scenarios. For example, in the case of a loader using a single thread to load data, it may not be possible to utilize a large cluster optimally. In these cases, it may be necessary to increase the number of threads or run multiple loaders in parallel.

1. Note that `INSERT .. ON CONFLICT` statements are not yet fully optimized as of YugabyteDB v2.2, so it is recommended to use basic `INSERT` statements if possible.

### Indexes during data load

As of YugabyteDB v2.2, it is recommended to create indexes before loading the data.

{{< note title="Note" >}}
This recommendation is subject to change in the near future with the introduction of online index rebuilds, which enables creating indexes after loading all the data.
{{< /note >}}

### Disable constraints and triggers temporarily

While loading data that is exported from another RDBMS, the source data set may not necessarily need to be checked for relational integrity because this was already performed when inserting into the source database. In such cases, disable checks such as FOREIGN KEY constraints, as well as triggers if possible. This would reduce the number of steps the database needs to perform while inserting data, which would speed up data loading.

## Import PostgreSQL data

{{< tip title="Migrate using YugabyteDB Voyager" >}}
To automate your migration from PostgreSQL to YugabyteDB, use [YugabyteDB Voyager](../../../migrate/). To learn more, refer to the [import schema](../../../migrate/migrate-steps/#import-schema) and [import data](../../../migrate/migrate-steps/#import-data) steps.
{{< /tip >}}

### Import data from CSV files

To import data that was previously exported into CSV files, use the `COPY FROM` command as follows:

```sql
COPY <table_name>
    FROM '<table_name>.csv'
    WITH (FORMAT CSV DELIMITER ',', HEADER, ROWS_PER_TRANSACTION 1000, DISABLE_FK_CHECK);
```

In the command above, the `ROWS_PER_TRANSACTION` parameter splits the load into smaller transactions (1000 rows each in this example), instead of running a single transaction spawning across all the data in the file. Additionally, the `DISABLE_FK_CHECK` parameter skips the foreign key checks for the duration of the import process.

Both `ROWS_PER_TRANSACTION` and `DISABLE_FK_CHECK` parameters are recommended for the initial import of the data, especially for large tables, because they significantly reduce the total time required to import the data. You can import multiple files in a single `COPY` command to further speed up the process. Following is a sample example:

```sql
yugabyte=# \! ls t*.txt
t1.txt	t2.txt	t3.txt
```

```output
yugabyte=# \! cat t*.txt
1,2,3
4,5,6
7,8,9
```

```sql
yugabyte=# \d t
```

```output
                 Table "public.t"
 Column |  Type   | Collation | Nullable | Default
--------+---------+-----------+----------+---------
 c1     | integer |           |          |
 c2     | integer |           |          |
 c3     | integer |           |          |
```

```sql
yugabyte=# SELECT * FROM t;
```

```output
 c1 | c2 | c3
----+----+----
(0 rows)
```

```sql
yugabyte=# COPY t FROM PROGRAM 'cat /home/yugabyte/t*.txt' WITH (FORMAT CSV, DELIMITER ',', ROWS_PER_TRANSACTION 1000, DISABLE_FK_CHECK);
COPY 3
```

```sql
yugabyte=# SELECT * FROM t;
```

```output
 c1 | c2 | c3
----+----+----
  7 |  8 |  9
  4 |  5 |  6
  1 |  2 |  3
(3 rows)
```

For detailed information on the COPY FROM command, refer to the [COPY](../../../api/ysql/the-sql-language/statements/cmd_copy/) statement reference.

#### Error handling

If the `COPY FROM` command fails during the process, you should try rerunning it. However, you don’t have to rerun the entire file. `COPY FROM` imports data into rows individually, starting from the top of the file. So if you know that some of the rows have been successfully imported prior to the failure, you can safely ignore those rows by adding the `SKIP` parameter.

For example, to skip the first 5000 rows in a file, run the command as follows:

```sql
COPY <table_name>
    FROM '<table_name>.csv'
    WITH (FORMAT CSV DELIMITER ',', HEADER, ROWS_PER_TRANSACTION 1000, DISABLE_FK_CHECK, SKIP 5000);
```

### Import data from SQL script

To import an entire database from a `pg_dump` or `ysql_dump` export, use `ysqlsh` as follows:

```sql
ysqlsh -f <database_name>.sql
```

{{< note title="Note" >}}

After the data import step, remember to recreate any constraints and triggers that might have been disabled to speed up loading the data. This ensures that the database will perform relational integrity checking for data going forward.

{{< /note >}}

## Verify a migration

Following are some steps that can be verified to ensure that the migration was successful.

<!-- ### Verify database objects

- Verify that all the tables and indexes have been created in YugabyteDB.
- Ensure that triggers and constraints are migrated and are working as expected. -->

### Verify row counts for tables

Run a `COUNT(*)` command to verify that the total number of rows match between the source database and YugabyteDB.

#### Run count query in YSQL

This can be done as shown below using a PLPGSQL function:

**Step 1.** Create the following function to print the number of rows in a single table.

```sql
create function
cnt_rows(schema text, tablename text) returns integer
as
$body$
declare
  result integer;
  query varchar;
begin
  query := 'SELECT count(1) FROM ' || schema || '.' || tablename;
  execute query into result;
  return result;
end;
$body$
language plpgsql;
```

**Step 2.** Run the following command to print the sizes of all tables in the database.

```sql
SELECT cnt_rows(table_schema, table_name)
    FROM information_schema.tables
    WHERE table_schema NOT IN ('pg_catalog', 'information_schema')
    AND table_type='BASE TABLE'
    ORDER BY 3 DESC;
```

#### Example

Following is an example illustrating the output of running the previous example on the [Northwind](../../../sample-data/northwind/#about-the-northwind-sample-database) database.

```sql
example=# SELECT cnt_rows(table_schema, table_name)
    FROM information_schema.tables
    WHERE table_schema NOT IN ('pg_catalog', 'information_schema')
    AND table_type='BASE TABLE'
    ORDER BY 3 DESC;
```

```output
 table_schema |       table_name       | cnt_rows
--------------+------------------------+----------
 public       | order_details          |     2155
 public       | orders                 |      830
 public       | customers              |       91
 public       | products               |       77
 public       | territories            |       53
 public       | us_states              |       51
 public       | employee_territories   |       49
 public       | suppliers              |       29
 public       | employees              |        9
 public       | categories             |        8
 public       | shippers               |        6
 public       | region                 |        4
 public       | customer_customer_demo |        0
 public       | customer_demographics  |        0
(14 rows)
```

The COUNT(*) query may time out in case of large tables. The following two options are recommended for such use cases.

**Option 1.** : Create a function and execute the query using the function which uses an implicit cursor.

```sql
CREATE OR REPLACE FUNCTION row_count(tbl regclass)
    RETURNS setof int AS
$func$
DECLARE
    _id int;
BEGIN
    FOR _id IN
        EXECUTE 'SELECT 1 FROM ' || tbl
    LOOP
        RETURN NEXT _id;
    END LOOP;
END
$func$ LANGUAGE plpgsql;
```

In this case, the query would be:

```sql
select count(*) from row_count('tablename');
```

Note that this query may take sometime to complete. You can also increase the client side timeout to something higher, maybe 10 minutes using the YB-TServer gflag: `--client_read_write_timeout_ms=600000`.

The following example is another workaround for running COUNT(*) in ysqlsh:

```sql
create table test (id int primary key, fname text);
insert into test select i, 'jon' || i from generate_series(1, 1000000) as i;
create table dual (test int);
insert into dual values (1);
explain select count(*) from test cross join dual;
```

```output
                                QUERY PLAN
---------------------------------------------------------------------------
 Aggregate  (cost=15202.50..15202.51 rows=1 width=8)
   ->  Nested Loop  (cost=0.00..12702.50 rows=1000000 width=0)
         ->  Seq Scan on test  (cost=0.00..100.00 rows=1000 width=0)
         ->  Materialize  (cost=0.00..105.00 rows=1000 width=0)
               ->  Seq Scan on dual  (cost=0.00..100.00 rows=1000 width=0)
```

**Option 2.** : Use [yb_hash_code()](../../../api/ysql/exprs/func_yb_hash_code/) to run different queries that work on different parts of the table, and control the parallelism at the application level.

Refer to [Distributed parallel queries](../../../api/ysql/exprs/func_yb_hash_code/#distributed-parallel-queries) for additional information on running COUNT(*) on tables using yb_hash_code().

#### Run count query in YCQL

In YCQL, the count() query can be executed using the [ycrc](https://github.com/yugabyte/yb-tools/tree/main/ycrc) tool.

The tool uses the exposed hash_partition function in order to execute smaller, more manageable queries which have are individually less resource intensive, and so they don't time out.

Following are the steps to set up and run the ycrc tool:

1. Download the [ycrc](https://github.com/yugabyte/yb-tools/tree/main/ycrc) tool by compiling the source from the GitHub repository.

1. Run `./ycrc --help` to confirm if the ycrc tool is working.

    ```sh
    ./ycrc --help
    ```

    ```output
    YCQL Row Count (ycrc) parallelizes counting the number of rows in a table for YugabyteDB CQL, allowing count(*) on tables that otherwise would fail with query timeouts

    Usage:

     ycrc <keyspace> [flags]

    Flags:

     -d, --debug  Verbose logging
     -h, --help  help for ycrc
     -c, --hosts strings  Cluster to connect to (default [127.0.0.1])
     -p, --parallel int  Number of concurrent tasks (default 16)
     --password string  user password
     -s, --scale int  Scaling factor of tasks per table, an int between 1 and 10 (default 6)
     --sslca string  SSL root ca path
     --sslcert string  SSL cert path
     --sslkey string  SSL key path
     --tables strings  List of tables inside of the keyspace - default to all
     -t, --timeout int  Timeout of a single query, in ms (default 1500)
     -u, --user string  database user (default "cassandra")
     --verify  Strictly verify SSL host (off by default)
     -v, --version  version for ycrc

    ```

1. Run the ycrc tool to count the rows in a given keyspace. The following example shows ycrc command to count rows in all tables in example keyspace:

    ```cql
    ./ycrc -c 1127.0.0.1 example
    ```

    ```output
    Checking table row counts for keyspace: example
    Checking row counts for: example.sensordata
    Partitioning columns for example.sensordata:(customer_name,device_id)

    Performing 4096 checks for example.sensordata with 16 parallel tasks

    Total time: 261 ms

    ==========
    Total Row Count example.sensordata = 60
    Checking row counts for: example.emp
    Partitioning columns for example.emp:(emp_id)

    Performing 4096 checks for example.emp with 16 parallel tasks

    Total time: 250 ms

    ==========
    Total Row Count example.emp = 3
    ```

    The following is an example with additional flags on the keyspace `example`:

    ```sh
    ./ycrc example \
          -c 127.0.0.1 \                         # Cluster to connect to (default [127.0.0.1])
          -u test \                              # database user (default "cassandra")
          --verify --password xxx \              # user password
          --tables sensordata \                  # List of tables inside of the keyspace - default to all
          -s 7 \                                 # Scaling factor of tasks per table, an int between 1 and 10 (default 6)
          -p 32 \                                # Number of concurrent tasks (default 16)
          -t 3000 \                              # Timeout of a single query, in ms (default 1500)
          --sslca /opt/yugabyte/certs/tests.crt  # This flag needs to specified if client to node authentication is enabled.
    ```

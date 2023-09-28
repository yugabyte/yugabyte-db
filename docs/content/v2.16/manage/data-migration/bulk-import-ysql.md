---
title: Bulk import
headerTitle: Bulk import for YSQL
linkTitle: Bulk import
description: Import data from PostgreSQL into YugabyteDB for YSQL.
menu:
  v2.16:
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
To automate your migration from PostgreSQL to YugabyteDB, use [YugabyteDB Voyager](/preview/yugabyte-voyager/). To learn more, refer to the [import schema](/preview/yugabyte-voyager/migrate/migrate-steps/#import-schema) and [import data](/preview/yugabyte-voyager/migrate/migrate-steps/#import-data) steps.
{{< /tip >}}

### Import data from CSV files

To import data that was previously exported into CSV files, use the `COPY FROM` command as follows:

```sql
COPY <table_name>
    FROM '<table_name>.csv'
    WITH (FORMAT CSV DELIMITER ',', HEADER, DISABLE_FK_CHECK);
```

In the command above, the `DISABLE_FK_CHECK` parameter skips the foreign key checks for the duration of the import process. Providing `DISABLE_FK_CHECK` parameter is recommended for the initial import of the data, especially for large tables, because it reduces the total time required to import the data.

To further speed up the process, you can import multiple files in a single COPY command. Following is a sample example:

```sql
yugabyte=# \! ls t*.txt
t1.txt t2.txt t3.txt
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
yugabyte=# COPY t FROM PROGRAM 'cat /home/yugabyte/t*.txt' WITH (FORMAT CSV, DELIMITER ',', DISABLE_FK_CHECK);
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

If the `COPY FROM` command fails during the process, you should try rerunning it. However, you don't have to rerun the entire file. `COPY FROM` imports data into rows individually, starting from the top of the file. So if you know that some of the rows have been successfully imported prior to the failure, you can safely ignore those rows by adding the `SKIP` parameter.

For example, to skip the first 5000 rows in a file, run the command as follows:

```sql
COPY <table_name>
    FROM '<table_name>.csv'
    WITH (FORMAT CSV DELIMITER ',', HEADER, DISABLE_FK_CHECK, SKIP 5000);
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

### Verify database objects

- Verify that all the tables and indexes have been created in YugabyteDB.
- Ensure that triggers and constraints are migrated and are working as expected.

### Verify row counts for tables

Run a `COUNT(*)` command to verify that the total number of rows match between the source database and YugabyteDB.

Use a PLPGSQL function to do the following:

1. Create the following function to print the number of rows in a single table:

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

1. Run the following command to print the sizes of all tables in the database.

    ```sql
    SELECT cnt_rows(table_schema, table_name)
        FROM information_schema.tables
        WHERE table_schema NOT IN ('pg_catalog', 'information_schema')
        AND table_type='BASE TABLE'
        ORDER BY 3 DESC;
    ```

The following example shows the output of running the previous example on the [Northwind](../../../sample-data/northwind/#about-the-northwind-sample-database) database.

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

#### Timeouts

The `COUNT(*)` query may time out in case of large tables. The following two options are recommended for such use cases:

**Option 1**: Create a function and execute the query using the function which uses an implicit cursor.

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

This query may take some time to complete. You can increase the client-side timeout to something higher, such as 10 minutes, using the YB-TServer flag `--client_read_write_timeout_ms=600000`.

The following example is another workaround for running `COUNT(*)` in ysqlsh:

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

**Option 2**: Use [`yb_hash_code()`](../../../api/ysql/exprs/func_yb_hash_code/) to run different queries that work on different parts of the table and control the parallelism at the application level.

Refer to [Distributed parallel queries](../../../api/ysql/exprs/func_yb_hash_code/#distributed-parallel-queries) for additional information on running `COUNT(*)` on tables using `yb_hash_code()`.

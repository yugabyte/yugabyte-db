---
title: Bulk import YSQL
headerTitle: Import data
linkTitle: Import data
description: Import data from PostgreSQL into YugabyteDB for YSQL.
badges: ysql
menu:
  preview:
    identifier: manage-bulk-import-ysql
    parent: manage-bulk-import-export
    weight: 729
type: docs
---

{{<api-tabs>}}

Use the following steps to manually migrate PostgreSQL data and applications to YugabyteDB after [exporting PostgreSQL data](../../data-migration/bulk-export-ysql/):

- [Prepare a cluster](#prepare-a-cluster)
- [Import PostgreSQL data](#import-postgresql-data)
- [Verify a migration](../verify-migration-ysql)

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

It is recommended to create indexes before loading the data.

{{< note title="Note" >}}
This recommendation is subject to change in the near future with the introduction of online index rebuilds, which enables creating indexes after loading all the data.
{{< /note >}}

### Disable constraints and triggers temporarily

While loading data that is exported from another RDBMS, the source data set may not necessarily need to be checked for relational integrity because this was already performed when inserting into the source database. In such cases, disable checks such as FOREIGN KEY constraints, as well as triggers if possible. This would reduce the number of steps the database needs to perform while inserting data, which would speed up data loading.

## Import PostgreSQL data

{{< tip title="Migrate using YugabyteDB Voyager" >}}
To automate your migration from PostgreSQL to YugabyteDB, use [YugabyteDB Voyager](../../../yugabyte-voyager/). To learn more, refer to the [import schema](../../../yugabyte-voyager/migrate/migrate-steps/#import-schema) and [import data](../../../yugabyte-voyager/migrate/migrate-steps/#import-data) steps.
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

## Verify migration

After the data and schema have been migrated, follow the steps in [Verify migration](../verify-migration-ysql) to ensure the migration was successful.

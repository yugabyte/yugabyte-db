---
title: Export PostgreSQL data
headerTitle: Export PostgreSQL data
linkTitle: Export PostgreSQL data
description: Steps for exporting PostgreSQL data for importing into YugabyteDB.
aliases:
  - /preview/migrate/migrate-from-postgresql/export-data/
menu:
  preview:
    identifier: migrate-postgresql-export
    parent: manual-import
    weight: 203
type: docs
---


The recommended way to export data from PostgreSQL for purposes of importing it to YugabyteDB is via CSV files using the COPY command.
However, for exporting an entire database that consists of smaller datasets, you use the YugabyteDB [`ysql_dump`](../../../admin/ysql-dump/) utility.

## Export data into CSV files using the COPY command

To export the data, connect to the source PostgreSQL database using the psql tool, and execute the COPY TO command as follows:

```sql
COPY <table_name>
    TO '<table_name>.csv'
    WITH (FORMAT CSV DELIMITER ',' HEADER);
```

{{< note title="Note" >}}

The COPY TO command exports a single table, so you should execute it for every table that you want to export.

{{< /note >}}

It is also possible to export a subset of rows based on a condition:

```sql
COPY (
    SELECT * FROM <table_name>
    WHERE <condition>
)
TO '<table_name>.csv'
WITH (FORMAT CSV DELIMITER ',' HEADER);
```

For all available options provided by the COPY TO command, refer to the [PostgreSQL documentation](https://www.postgresql.org/docs/current/sql-copy.html).

### Parallelize large table export

For large tables, it might be beneficial to parallelize the process by exporting data in chunks as follows:

```sql
COPY (
    SELECT * FROM <table_name>
    ORDER BY <primary_key_col>
    LIMIT num_rows_per_export OFFSET 0
)
TO '<table_name>_1.csv'
WITH (FORMAT CSV DELIMITER ',' HEADER);
```

```sql
COPY (
    SELECT * FROM <table_name>
    ORDER BY <primary_key_col>
    LIMIT num_rows_per_export OFFSET num_rows_per_export
)
TO '<table_name>_2.csv'
WITH (FORMAT CSV DELIMITER ',' HEADER);
```

```sql
COPY (
    SELECT * FROM <table_name>
    ORDER BY <primary_key_col>
    LIMIT num_rows_per_export OFFSET num_rows_per_export * 2
)
TO '<table_name>_3.csv'
WITH (FORMAT CSV DELIMITER ',' HEADER);
```

You can run the above commands in parallel to speed up the process. This approach will also produce multiple CSV files, allowing for parallel import on the YugabyteDB side.

## Export data into SQL script using ysql_dump

An alternative way to export the data is using the YugabyteDB [`ysql_dump`](../../../admin/ysql-dump/) backup utility, which is derived from PostgreSQL pg_dump.

```sh
$ ysql_dump -d <database_name> > <database_name>.sql
```

`ysql_dump` is the ideal option for smaller datasets, because it allows you to export a whole database by running a single command. However, the COPY command is recommended for large databases, because it significantly enhances the performance.

<!--

## Exporting an entire database

The recommended way to dump an entire database from PostgreSQL is to use the YugabyteDB [`ysql_dump`](../../../admin/ysql-dump/) backup utility, which is in turn derived from PostgreSQL pg_dump.

```sh
$ ysql_dump -d mydatabase > mydatabase-dump.sql
```

{{< note title="Note" >}}
The `ysql_dump` approach has been tested on PostgreSQL v11.2, and may not work on very new versions of PostgreSQL. To export an entire database in these cases, use the pg_dump tool, which is documented in detail in the [PostgreSQL documentation on pg_dump](https://www.postgresql.org/docs/12/app-pgdump.html).
{{< /note >}}

## Export using COPY

This is an alternative to using ysql_dump in order to export a single table from the source PostgreSQL database into CSV files. This tool allows extracting a subset of rows and/or columns from a table. This can be achieved by connecting to the source DB using psql and using the `COPY TO` command, as shown below.

```sql
COPY mytable TO 'export-1.csv' DELIMITER ',' CSV HEADER;
```

To extract a subset of rows from a table, it is possible to output the result of an SQL command.

```sql
COPY (
  SELECT * FROM mytable
    WHERE <where condition>
) TO 'export-1.csv' DELIMITER ',' CSV HEADER;
```

The various options here are described in detail in the [PostgreSQL documentation for the COPY command](https://www.postgresql.org/docs/12/sql-copy.html).

## Run large table exports in parallel

Exporting large data sets from PostgreSQL can be made efficient by running multiple COPY processes in parallel for a subset of data. This will result in multiple csv files being produced, which can subsequently be imported in parallel.

An example of running multiple exports in parallel is shown below. Remember to use a suitable value for *num_rows_per_export*, for example 1 million rows.

```sql
COPY (
  SELECT * FROM mytable
    ORDER BY primary_key_col
    LIMIT num_rows_per_export OFFSET 0
) TO 'export-1.csv' DELIMITER ',' CSV HEADER;

COPY (
  SELECT * FROM mytable
    ORDER BY primary_key_col
    LIMIT num_rows_per_export OFFSET num_rows_per_export
) TO 'export-2.csv' WITH CSV;

...
``` -->

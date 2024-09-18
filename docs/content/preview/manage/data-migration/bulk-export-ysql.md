---
title: Bulk export YSQL
headerTitle: Export data
linkTitle: Export data
description: Bulk export for YSQL using ysql_dump.
badges: ysql
menu:
  preview:
    identifier: manage-bulk-export-ysql
    parent: manage-bulk-import-export
    weight: 719
type: docs
---

{{<api-tabs>}}

The recommended way to export data from PostgreSQL for purposes of importing it to YugabyteDB is via CSV files using the COPY command.

To export an entire database that consists of smaller datasets, you can also use the YugabyteDB [ysql_dump](../../../admin/ysql-dump/) utility.

{{< tip title="Migrate using YugabyteDB Voyager" >}}
To automate your migration from PostgreSQL to YugabyteDB, use [YugabyteDB Voyager](/preview/yugabyte-voyager/). To learn more, refer to the [export schema](/preview/yugabyte-voyager/migrate/migrate-steps/#export-schema) and [export data](/preview/yugabyte-voyager/migrate/migrate-steps/#export-data) steps.
{{< /tip >}}

## Export data into CSV files using the COPY command

To export the data, connect to the source PostgreSQL database using the psql tool, and execute the COPY TO command as follows:

```sql
COPY <table_name>
    TO '<table_name>.csv'
    WITH (FORMAT CSV, HEADER false, DELIMITER ',');
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
WITH (FORMAT CSV, HEADER false, DELIMITER ',');
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
WITH (FORMAT CSV, HEADER false, DELIMITER ',');
```

```sql
COPY (
    SELECT * FROM <table_name>
    ORDER BY <primary_key_col>
    LIMIT num_rows_per_export OFFSET num_rows_per_export
)
TO '<table_name>_2.csv'
WITH (FORMAT CSV, HEADER false, DELIMITER ',');
```

```sql
COPY (
    SELECT * FROM <table_name>
    ORDER BY <primary_key_col>
    LIMIT num_rows_per_export OFFSET num_rows_per_export * 2
)
TO '<table_name>_3.csv'
WITH (FORMAT CSV, HEADER false, DELIMITER ',');
```

You can run the above commands in parallel to speed up the process. This approach will also produce multiple CSV files, allowing for parallel import on the YugabyteDB side.

## Export data into SQL script using ysql_dump

An alternative way to export the data is using the YugabyteDB [ysql_dump](../../../admin/ysql-dump/) backup utility, which is derived from PostgreSQL pg_dump.

```sh
$ ysql_dump -d <database_name> > <database_name>.sql
```

ysql_dump is the ideal option for smaller datasets, because it allows you to export a whole database by running a single command. However, the COPY command is recommended for large databases, because it significantly enhances the performance.

## Next step

- [Bulk import](../bulk-import-ysql/)

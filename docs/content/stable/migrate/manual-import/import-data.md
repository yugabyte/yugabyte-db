---
title: Import PostgreSQL data
headerTitle: Import PostgreSQL data
linkTitle: Import PostgreSQL data
description: Steps for importing PostgreSQL data into YugabyteDB.
menu:
  stable:
    identifier: migrate-postgresql-import-data
    parent: manual-import
    weight: 205
type: docs
---

{{< tip title="Migrate using YugabyteDB Voyager" >}}
To automate your migration from PostgreSQL to YugabyteDB, use [YugabyteDB Voyager](../../yb-voyager/). To learn more, refer to the [import schema](../../yb-voyager/migrate-steps/#import-schema) and [import data](../../yb-voyager/migrate-steps/#import-data) steps.
{{< /tip >}}

## Import data from CSV files

To import data that was previously exported into CSV files, use the COPY FROM command as follows:

```sql
COPY <table_name>
    FROM '<table_name>.csv'
    WITH (FORMAT CSV DELIMITER ',', HEADER, ROWS_PER_TRANSACTION 1000, DISABLE_FK_CHECK);
```

In the command above, the `ROWS_PER_TRANSACTION` parameter splits the load into smaller transactions (1000 rows each in this example), instead of running a single transaction spawning across all the data in the file. Additionally, the `DISABLE_FK_CHECK` parameter skips the foreign key checks for the duration of the import process.

Both `ROWS_PER_TRANSACTION` and `DISABLE_FK_CHECK` parameters are recommended for the initial import of the data, especially for large tables, because they significantly reduce the total time required to import the data. You can import multiple files in a single COPY command to further speed up the process. Following is a sample example:

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

### Error handling

If the COPY FROM command fails during the process, you should try rerunning it. However, you don’t have to rerun the entire file. COPY FROM imports data into rows individually, starting from the top of the file. So if you know that some of the rows have been successfully imported prior to the failure, you can safely ignore those rows by adding the SKIP parameter.

For example, to skip the first 5000 rows in a file, run the command as follows:

```sql
COPY <table_name>
    FROM '<table_name>.csv'
    WITH (FORMAT CSV DELIMITER ',', HEADER, ROWS_PER_TRANSACTION 1000, DISABLE_FK_CHECK, SKIP 5000);
```

## Import data from SQL script

To import an entire database from a `pg_dump` or `ysql_dump` export, use `ysqlsh` as follows:

```sql
ysqlsh -f <database_name>.sql
```

{{< note title="Note" >}}

After the data import step, remember to recreate any constraints and triggers that might have been disabled to speed up loading the data. This ensures that the database will perform relational integrity checking for data going forward.

{{< /note >}}

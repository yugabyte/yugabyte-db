---
title: Import PostgreSQL data
headerTitle: Import PostgreSQL data
linkTitle: Import PostgreSQL data
description: Steps for importing PostgreSQL data into YugabyteDB.
aliases:
  - /preview/migrate/migrate-from-postgresql/import-data/
menu:
  preview:
    identifier: migrate-postgresql-import-data
    parent: manual-import
    weight: 205
type: docs
---


## Import data from CSV files

To import data that was previously exported into CSV files, use the COPY FROM command as follows:

```sql
COPY <table_name>
    FROM ‘<table_name>.csv’
    WITH (
        FORMAT CSV DELIMITER ',' HEADER
        ROWS_PER_TRANSACTION 1000
        DISABLE_FK_CHECK
    );
```

In the command above, the `ROWS_PER_TRANSACTION` parameter splits the updates into smaller transactions (1000 rows each in this example), instead of running a single transaction spawning across all the data in the file. Additionally, the `DISABLE_FK_CHECK` parameter skips the foreign key checks for the duration of the import process.

Both `ROWS_PER_TRANSACTION` and `DISABLE_FK_CHECK` parameters are recommended for the initial import of the data, especially for large tables, because they significantly reduce the total time required to import the data. If you imported the data into multiple CSV files, you need to run the command for every file. You can import multiple files in parallel to further speed up the process.

For detailed information on the COPY FROM command, refer to the [COPY](../../../api/ysql/the-sql-language/statements/cmd_copy/) statement reference.

### Error handling

If the COPY FROM command fails during the process, you should try rerunning it. However, you don’t have to rerun the entire file. COPY FROM imports data into rows individually, starting from the top of the file. So if you know that some of the rows have been successfully imported prior to the failure, you can safely ignore those rows by adding the SKIP parameter.

For example, to skip the first 5000 rows in a file, run the command as follows:

```sql
COPY <table_name>
    FROM ‘<table_name>.csv’
    WITH (
        FORMAT CSV DELIMITER ',' HEADER
        ROWS_PER_TRANSACTION 1000
        DISABLE_FK_CHECK
        SKIP 5000
    );
```

## Import data from SQL script

To import an entire database from a `pg_dump` or `ysql_dump` export, use `ysqlsh` as follows:

```sql
ysqlsh -f <database_name>.sql
```

{{< note title="Note" >}}

After the data import step, remember to recreate any constraints and triggers that might have been disabled to speed up loading the data. This ensures that the database will perform relational integrity checking for data going forward.

{{< /note >}}

---
title: Verify migration
linkTitle: Verify migration
description: Run steps to ensure a successful migration.
menu:
  preview:
    identifier: verify-migration
    parent: perform-migration-1
    weight: 504
isTocNested: true
showAsideToc: true
---


After the successful execution of the `yb-voyager import data` command, the automated part of the database migration process is considered complete. You should manually run validation queries on both the source and target database to ensure that the data is correctly migrated. A sample query to validate the databases can include checking the row count of each table.

The following steps can be performed to ensure that the migration was successful.

## Verify database objects

* Verify that all the tables and indexes have been created in YugabyteDB
* Ensure that triggers and constraints are migrated and are working as expected

## Verify row counts for tables

Run a `COUNT(*)` command to verify that the total number of rows match between the source database and YugabyteDB. This can be done as shown below using a PLPGSQL function.

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

### Example

An example illustrating the output of running the above on the Northwind database is as follows:

```output
example=# SELECT cnt_rows(table_schema, table_name)
    FROM information_schema.tables
    WHERE table_schema NOT IN ('pg_catalog', 'information_schema')
    AND table_type='BASE TABLE'
    ORDER BY 3 DESC;

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

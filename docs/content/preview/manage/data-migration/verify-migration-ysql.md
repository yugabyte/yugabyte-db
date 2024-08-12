---
title: Verify migration for YSQL
headerTitle: Verify migration
linkTitle: Verify migration
description: Verify if the migration was successful
badges: ysql
menu:
  preview:
    identifier: verify-migration-ysql
    parent: manage-bulk-import-export
    weight: 739
type: docs
---

{{<api-tabs>}}

What follows are some steps that you can use to verify that your migration was successful.

## Verify database objects

- Verify that all the tables and indexes have been created in YugabyteDB.
- Ensure that triggers and constraints are migrated and are working as expected.

## Verify row counts for tables

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

### Timeouts

The `COUNT(*)` query may time out in case of large tables. The following two options are recommended for such use cases.

#### Create a function

Create a function and execute the query using the function which uses an implicit cursor.

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

#### yb_hash_code function

Use [`yb_hash_code()`](../../../api/ysql/exprs/func_yb_hash_code/) to run different queries that work on different parts of the table and control the parallelism at the application level.

Refer to [Distributed parallel queries](../../../api/ysql/exprs/func_yb_hash_code/#distributed-parallel-queries) for additional information on running `COUNT(*)` on tables using `yb_hash_code()`.

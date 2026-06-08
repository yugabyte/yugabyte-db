---
title: ALTER AGGREGATE statement [YSQL]
headerTitle: ALTER AGGREGATE
linkTitle: ALTER AGGREGATE
description: Use the ALTER AGGREGATE statement to change the definition of an aggregate function.
menu:
  stable_api:
    identifier: ddl_alter_aggregate
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER AGGREGATE` statement to change the schema of an existing aggregate function.

## Syntax

{{%ebnf%}}
  alter_aggregate,
  alter_aggregate_action,
  aggregate_signature,
  arg_mode,
  arg_name,
  arg_type
{{%/ebnf%}}

You must identify the aggregate to be altered by its name, schema, and signature. The signature must include the data types of the function's arguments.

## Semantics

### SET SCHEMA *schema_name*

Move the aggregate function to a different schema. The function is renamed within its original schema, but all dependent objects are maintained correctly.

For more details, see the semantics in the [PostgreSQL documentation](https://www.postgresql.org/docs/15/sql-alteraggregate.html).

## Examples

- Create an aggregate function and move it to a different schema:

    ```plpgsql
    create schema s1;
    create schema s2;    

    -- Create a simple aggregate
    create aggregate s1.my_agg(int) (
      stype = int,
      sfunc = int4pl,
      initcond = '0'
    );    

    -- Move the aggregate to s2
    alter aggregate s1.my_agg(int) set schema s2;    

    -- Verify the change
    select n.nspname as schema, p.proname as aggregate
    from pg_proc p
    join pg_namespace n on p.pronamespace = n.oid
    where p.proname = 'my_agg';
    ```

    You should see an output similar to the following:

    ```output
     schema | aggregate
    --------+-----------
     s2     | my_agg
    ```

- Rename an aggregate.

    ```plpgsql
    yugabyte=# ALTER AGGREGATE sumdouble (float8) RENAME TO other_sumdouble;
    ```

- Change the owner.

    ```plpgsql
    yugabyte=# ALTER AGGREGATE sumdouble (float8) OWNER TO yugabyte;
    yugabyte=# ALTER AGGREGATE sumdouble (float8) OWNER TO CURRENT_ROLE;
    yugabyte=# ALTER AGGREGATE sumdouble (float8) OWNER TO CURRENT_USER;
    yugabyte=# ALTER AGGREGATE sumdouble (float8) OWNER TO SESSION_USER;
    ```

- Change the schema.

    ```plpgsql
    yugabyte=# ALTER AGGREGATE sumdouble (float8) SET SCHEMA public;
    ```

## See also

- [CREATE AGGREGATE](../ddl_create_aggregate)
- [DROP AGGREGATE](../ddl_drop_aggregate)

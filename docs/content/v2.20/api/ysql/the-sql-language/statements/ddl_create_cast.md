---
title: CREATE CAST statement [YSQL]
headerTitle: CREATE CAST
linkTitle: CREATE CAST
description: Use the CREATE CAST statement to create a cast.
menu:
  v2.20:
    identifier: ddl_create_cast
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE CAST` statement to create a cast.

## Syntax

{{%ebnf%}}
  create_cast,
  create_cast_with_function,
  create_cast_without_function,
  create_cast_with_inout,
  cast_signature
{{%/ebnf%}}

## Semantics

See the semantics of each option in the [PostgreSQL docs][postgresql-docs-create-cast].

## Examples

`WITH FUNCTION` example.

```plpgsql
yugabyte=# CREATE FUNCTION sql_to_date(integer) RETURNS date AS $$
             SELECT $1::text::date
             $$ LANGUAGE SQL IMMUTABLE STRICT;
yugabyte=# CREATE CAST (integer AS date) WITH FUNCTION sql_to_date(integer) AS ASSIGNMENT;
yugabyte=# SELECT CAST (3 AS date);
```

`WITHOUT FUNCTION` example.

```plpgsql
yugabyte=# CREATE TYPE myfloat4;
yugabyte=# CREATE FUNCTION myfloat4_in(cstring) RETURNS myfloat4
             LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'float4in';
yugabyte=# CREATE FUNCTION myfloat4_out(myfloat4) RETURNS cstring
             LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'float4out';
yugabyte=# CREATE TYPE myfloat4 (
             INPUT = myfloat4_in,
             OUTPUT = myfloat4_out,
             LIKE = float4
           );
yugabyte=# SELECT CAST('3.14'::myfloat4 AS float4);
yugabyte=# CREATE CAST (myfloat4 AS float4) WITHOUT FUNCTION;
yugabyte=# SELECT CAST('3.14'::myfloat4 AS float4);
```

`WITH INOUT` example.

```plpgsql
yugabyte=# CREATE TYPE myint4;
yugabyte=# CREATE FUNCTION myint4_in(cstring) RETURNS myint4
             LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'int4in';
yugabyte=# CREATE FUNCTION myint4_out(myint4) RETURNS cstring
             LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'int4out';
yugabyte=# CREATE TYPE myint4 (
             INPUT = myint4_in,
             OUTPUT = myint4_out,
             LIKE = int4
           );
yugabyte=# SELECT CAST('2'::myint4 AS int4);
yugabyte=# CREATE CAST (myint4 AS int4) WITH INOUT;
yugabyte=# SELECT CAST('2'::myint4 AS int4);
```

## See also

- [`DROP CAST`](../ddl_drop_cast)
- [postgresql-docs-create-cast](https://www.postgresql.org/docs/current/sql-createcast.html)

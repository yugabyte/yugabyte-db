---
title: CREATE AGGREGATE statement [YSQL]
headerTitle: CREATE AGGREGATE
linkTitle: CREATE AGGREGATE
description: Use the CREATE AGGREGATE statement to create an aggregate function.
menu:
  v2.20:
    identifier: ddl_create_aggregate
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE AGGREGATE` statement to create an aggregate function.  There are three ways to
create aggregates.

## Syntax

{{%ebnf%}}
  create_aggregate,
  create_aggregate_normal,
  create_aggregate_order_by,
  create_aggregate_old,
  aggregate_arg,
  aggregate_normal_option,
  aggregate_order_by_option,
  aggregate_old_option
{{%/ebnf%}}

## Semantics

The order of options does not matter.  Even the mandatory options `BASETYPE`, `SFUNC`, and `STYPE`
may appear in any order.

See the semantics of each option in the [PostgreSQL docs][postgresql-docs-create-aggregate].

## Examples

Normal syntax example.

```plpgsql
yugabyte=# CREATE AGGREGATE sumdouble (float8) (
              STYPE = float8,
              SFUNC = float8pl,
              MSTYPE = float8,
              MSFUNC = float8pl,
              MINVFUNC = float8mi
           );
yugabyte=# CREATE TABLE normal_table(
             f float8,
             i int
           );
yugabyte=# INSERT INTO normal_table(f, i) VALUES
             (0.1, 9),
             (0.9, 1);
yugabyte=# SELECT sumdouble(f), sumdouble(i) FROM normal_table;
```

Order by syntax example.

```plpgsql
yugabyte=# CREATE AGGREGATE my_percentile_disc(float8 ORDER BY anyelement) (
             STYPE = internal,
             SFUNC = ordered_set_transition,
             FINALFUNC = percentile_disc_final,
             FINALFUNC_EXTRA = true,
             FINALFUNC_MODIFY = read_write
           );
yugabyte=# SELECT my_percentile_disc(0.1), my_percentile_disc(0.9)
             WITHIN GROUP (ORDER BY typlen)
             FROM pg_type;
```

Old syntax example.

```plpgsql
yugabyte=# CREATE AGGREGATE oldcnt(
             SFUNC = int8inc,
             BASETYPE = 'ANY',
             STYPE = int8,
             INITCOND = '0'
           );
yugabyte=# SELECT oldcnt(*) FROM pg_aggregate;
```

Zero-argument aggregate example.

```plpgsql
yugabyte=# CREATE AGGREGATE newcnt(*) (
             SFUNC = int8inc,
             STYPE = int8,
             INITCOND = '0',
             PARALLEL = SAFE
           );
yugabyte=# SELECT newcnt(*) FROM pg_aggregate;
```

## See also

- [`DROP AGGREGATE`](../ddl_drop_aggregate)
- [postgresql-docs-create-aggregate](https://www.postgresql.org/docs/current/sql-createaggregate.html)

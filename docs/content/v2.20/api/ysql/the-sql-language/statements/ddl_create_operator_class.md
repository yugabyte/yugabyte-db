---
title: CREATE OPERATOR CLASS statement [YSQL]
headerTitle: CREATE OPERATOR CLASS
linkTitle: CREATE OPERATOR CLASS
description: Use the CREATE OPERATOR CLASS statement to create an operator class.
menu:
  v2.20:
    identifier: ddl_create_operator_class
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE OPERATOR CLASS` statement to create an operator class.

## Syntax

{{%ebnf%}}
  create_operator_class,
  operator_class_as
{{%/ebnf%}}

## Semantics

See the semantics of each option in the [PostgreSQL docs][postgresql-docs-create-op-class].  See the
semantics of `strategy_number` and `support_number` in another page of the [PostgreSQL
docs][postgresql-docs-xindex].

## Examples

Basic example.

```plpgsql
yugabyte=# CREATE OPERATOR CLASS my_op_class
           FOR TYPE int4
           USING btree AS
           OPERATOR 1 <,
           OPERATOR 2 <=;
```

## See also

- [`DROP OPERATOR CLASS`](../ddl_drop_operator_class)
- [postgresql-docs-create-op-class](https://www.postgresql.org/docs/current/sql-createopclass.html)
- [postgresql-docs-xindex](https://www.postgresql.org/docs/current/xindex.html)

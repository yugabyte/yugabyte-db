---
title: DROP OPERATOR CLASS statement [YSQL]
headerTitle: DROP OPERATOR CLASS
linkTitle: DROP OPERATOR CLASS
description: Use the DROP OPERATOR CLASS statement to remove an operator class.
menu:
  stable:
    identifier: ddl_drop_operator_class
    parent: statements
type: docs
---

## Synopsis

Use the `DROP OPERATOR CLASS` statement to remove an operator class.

## Syntax

{{%ebnf%}}
  drop_operator_class
{{%/ebnf%}}

## Semantics

See the semantics of each option in the [PostgreSQL docs][postgresql-docs-drop-operator-class].

## Examples

Basic example.

```plpgsql
yugabyte=# CREATE OPERATOR CLASS my_op_class
           FOR TYPE int4
           USING btree AS
           OPERATOR 1 <,
           OPERATOR 2 <=;
yugabyte=# DROP OPERATOR CLASS my_op_class USING btree;
```

## See also

- [`CREATE OPERATOR CLASS`](../ddl_create_operator_class)
- [postgresql-docs-drop-operator-class](https://www.postgresql.org/docs/current/sql-dropopclass.html)

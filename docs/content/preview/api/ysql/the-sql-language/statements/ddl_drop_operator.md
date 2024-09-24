---
title: DROP OPERATOR statement [YSQL]
headerTitle: DROP OPERATOR
linkTitle: DROP OPERATOR
description: Use the DROP OPERATOR statement to remove an operator.
menu:
  preview:
    identifier: ddl_drop_operator
    parent: statements
aliases:
  - /preview/api/ysql/commands/ddl_drop_operator/
type: docs
---

## Synopsis

Use the `DROP OPERATOR` statement to remove an operator.

## Syntax

{{%ebnf%}}
  drop_operator,
  operator_signature
{{%/ebnf%}}

## Semantics

See the semantics of each option in the [PostgreSQL docs][postgresql-docs-drop-operator].

## Examples

Basic example.

```plpgsql
yugabyte=# CREATE OPERATOR @#@ (
             rightarg = int8,
             procedure = numeric_fac
           );
yugabyte=# DROP OPERATOR @#@ (NONE, int8);
```

## See also

- [`CREATE OPERATOR`](../ddl_create_operator)
- [postgresql-docs-drop-operator](https://www.postgresql.org/docs/current/sql-dropoperator.html)

---
title: CREATE OPERATOR statement [YSQL]
headerTitle: CREATE OPERATOR
linkTitle: CREATE OPERATOR
description: Use the CREATE OPERATOR statement to create an operator.
menu:
  v2.18:
    identifier: ddl_create_operator
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE OPERATOR` statement to create an operator.

## Syntax

{{%ebnf%}}
  create_operator,
  operator_option
{{%/ebnf%}}

## Semantics

See the semantics of each option in the [PostgreSQL docs][postgresql-docs-create-operator].

## Examples

Basic example.

```plpgsql
yugabyte=# CREATE OPERATOR @#@ (
             rightarg = int8,
             procedure = numeric_fac
           );
yugabyte=# SELECT @#@ 5;
```

```
 ?column?
----------
      120
```

## See also

- [`DROP OPERATOR`](../ddl_drop_operator)
- [postgresql-docs-create-operator](https://www.postgresql.org/docs/current/sql-createoperator.html)

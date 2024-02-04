---
title: DROP RULE statement [YSQL]
headerTitle: DROP RULE
linkTitle: DROP RULE
description: Use the DROP RULE statement to remove a rule.
menu:
  stable:
    identifier: ddl_drop_rule
    parent: statements
type: docs
---

## Synopsis

Use the `DROP RULE` statement to remove a rule.

## Syntax

{{%ebnf%}}
  drop_rule
{{%/ebnf%}}

## Semantics

See the semantics of each option in the [PostgreSQL docs][postgresql-docs-drop-rule].

## Examples

Basic example.

```plpgsql
yugabyte=# CREATE TABLE t1(a int4, b int4);
yugabyte=# CREATE TABLE t2(a int4, b int4);
yugabyte=# CREATE RULE t1_to_t2 AS ON INSERT TO t1 DO INSTEAD
             INSERT INTO t2 VALUES (new.a, new.b);
yugabyte=# DROP RULE t1_to_t2 ON t1;
```

## See also

- [`CREATE RULE`](../ddl_create_rule)
- [postgresql-docs-drop-rule](https://www.postgresql.org/docs/current/sql-droprule.html)

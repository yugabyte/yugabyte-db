---
title: CREATE RULE statement [YSQL]
headerTitle: CREATE RULE
linkTitle: CREATE RULE
description: Use the CREATE RULE statement to create a rule.
menu:
  v2.18:
    identifier: ddl_create_rule
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE RULE` statement to create a rule.

## Syntax

{{%ebnf%}}
  create_rule,
  rule_event,
  command
{{%/ebnf%}}

## Semantics

See the semantics of each option in the [PostgreSQL docs][postgresql-docs-create-rule].

## Examples

Basic example.

```plpgsql
yugabyte=# CREATE TABLE t1(a int4, b int4);
yugabyte=# CREATE TABLE t2(a int4, b int4);
yugabyte=# CREATE RULE t1_to_t2 AS ON INSERT TO t1 DO INSTEAD
             INSERT INTO t2 VALUES (new.a, new.b);
yugabyte=# INSERT INTO t1 VALUES (3, 4);
yugabyte=# SELECT * FROM t1;
```

```
 a | b
---+---
(0 rows)
```

```plpgsql
yugabyte=# SELECT * FROM t2;
```

```
 a | b
---+---
 3 | 4
(1 row)
```

## See also

- [`DROP RULE`](../ddl_drop_rule)
- [postgresql-docs-create-rule](https://www.postgresql.org/docs/current/sql-createrule.html)

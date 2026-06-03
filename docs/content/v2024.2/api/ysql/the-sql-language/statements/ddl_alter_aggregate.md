---
title: ALTER AGGREGATE statement [YSQL]
headerTitle: ALTER AGGREGATE
linkTitle: ALTER AGGREGATE
description: Use the ALTER AGGREGATE statement to change the definition of an aggregate function.
menu:
  v2024.2_api:
    identifier: ddl_alter_aggregate
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER AGGREGATE` statement to change the definition of an aggregate function.

## Syntax

{{%ebnf%}}
  alter_aggregate,
  alter_aggregate_action,
  aggregate_signature
{{%/ebnf%}}

## Semantics

See the semantics in the [PostgreSQL documentation][postgresql-docs-alter-aggregate].

## Examples

Rename an aggregate.

```plpgsql
yugabyte=# ALTER AGGREGATE sumdouble (float8) RENAME TO other_sumdouble;
```

Change the owner.

```plpgsql
yugabyte=# ALTER AGGREGATE sumdouble (float8) OWNER TO yugabyte;
yugabyte=# ALTER AGGREGATE sumdouble (float8) OWNER TO CURRENT_ROLE;
yugabyte=# ALTER AGGREGATE sumdouble (float8) OWNER TO CURRENT_USER;
yugabyte=# ALTER AGGREGATE sumdouble (float8) OWNER TO SESSION_USER;
```

## See also

- [CREATE AGGREGATE](../ddl_create_aggregate)
- [DROP AGGREGATE](../ddl_drop_aggregate)

[postgresql-docs-alter-aggregate]: https://www.postgresql.org/docs/15/sql-alteraggregate.html

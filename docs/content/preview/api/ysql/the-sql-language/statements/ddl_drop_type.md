---
title: DROP TYPE statement [YSQL]
headerTitle: DROP TYPE
linkTitle: DROP TYPE
description: Use the DROP TYPE statement to remove a user-defined type from the database.
menu:
  preview:
    identifier: ddl_drop_type
    parent: statements
aliases:
  - /preview/api/ysql/commands/ddl_drop_type/
type: docs
---

## Synopsis

Use the `DROP TYPE` statement to remove a user-defined type from the database.

## Syntax

{{%ebnf%}}
  drop_type
{{%/ebnf%}}

## Semantics

### *drop_type*

### *type_name*

Specify the name of the user-defined type to drop.

## Examples

Simple example

```plpgsql
yugabyte=# CREATE TYPE feature_struct AS (id INTEGER, name TEXT);
yugabyte=# DROP TYPE feature_struct;
```

`IF EXISTS` example

```plpgsql
yugabyte=# DROP TYPE IF EXISTS feature_shell;
```

`CASCADE` example

```plpgsql
yugabyte=# CREATE TYPE feature_enum AS ENUM ('one', 'two', 'three');
yugabyte=# CREATE TABLE feature_tab_enum (feature_col feature_enum);
yugabyte=# DROP TYPE feature_tab_enum CASCADE;
```

`RESTRICT` example

```plpgsql
yugabyte=# CREATE TYPE feature_range AS RANGE (subtype=INTEGER);
yugabyte=# CREATE TABLE feature_tab_range (feature_col feature_range);
yugabyte=# -- The following should error:
yugabyte=# DROP TYPE feature_range RESTRICT;
yugabyte=# DROP TABLE feature_tab_range;
yugabyte=# DROP TYPE feature_range RESTRICT;
```

## See also

- [`CREATE TYPE`](../ddl_create_type)

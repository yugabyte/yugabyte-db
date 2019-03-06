---
title: ALTER TABLE
linkTitle: ALTER TABLE
summary: Alter a table in a database
description: ALTER TABLE
menu:
  latest:
    identifier: api-ysql-commands-alter-table
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/ddl_alter_table
isTocNested: true
showAsideToc: true
---

## Synopsis
`ALTER TABLE` changes or redefines one or more attributes of a table.

## Syntax

### Diagram 

### Grammar
```
alter_table ::= ALTER TABLE [ IF EXISTS ] [ ONLY ] name [ * ] alter_table_action [, ... ]

alter_table_action ::=
  { ADD [ COLUMN ] [ IF NOT EXISTS ] column_name data_type |
    DROP [ COLUMN ] [ IF EXISTS ] column_name [ RESTRICT | CASCADE ] |
    ADD table_constraint |
    DROP CONSTRAINT [ IF EXISTS ]  constraint_name [ RESTRICT | CASCADE ] }

table_constraint ::=
  CONSTRAINT constraint_name { CHECK ( expression ) [ NO INHERIT ] |
                               PRIMARY KEY ( column_name [, ... ] ) index_parameters }
```

## Semantics

- An error is raised if specified table does not exist unless `IF EXIST` is used.
- `ADD COLUMN` adds new column.
- `DROP COLUMN` drops existing column.
- `ADD table_constraint` adds new table_constraint.
- `DROP table_constraint` drops existing table_constraint.
- Other `ALTER TABLE` options are not yet supported.

## See Also
[`CREATE TABLE`](../ddl_create_table)
[`DROP TABLE`](../ddl_drop_table)
[Other PostgreSQL Statements](..)

---
title: CREATE SCHEMA
linkTitle: CREATE SCHEMA
summary: Create schema
description: CREATE SCHEMA
menu:
  latest:
    identifier: api-ysql-commands-create-schema
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/cmd_create_schema
isTocNested: true
showAsideToc: true
---

## Synopsis
`CREATE SCHEMA` inserts a new schema definition to the current database. Schema name must be unique.

## Syntax

### Diagram 

### Grammar
```
create_schema ::= CREATE SCHEMA [ IF NOT EXISTS ] schema_name [ schema_element [ ... ] ]
```

Where
- `schema_name` specifies the schema to be created.

- `schema_element` is a SQL statement that `CREATE` a database object to be created within the schema.

## Semantics

- `AUTHORIZATION` clause is not yet supported.
- Only `CREATE TABLE`, `CREATE VIEW`, `CREATE INDEX`, `CREATE SEQUENCE`, `CREATE TRIGGER`, and `GRANT` can be use to create objects within `CREATE SCHEMA` statement. Other database objects must be created in separate commands after the schema is created.

## See Also
[`CREATE TABLE`](../ddl_create_table)
[`CREATE VIEW`](../ddl_create_view)
[`CREATE INDEX`](../ddl_create_index)
[`CREATE SEQUENCE`](../ddl_create_seq)
[Other PostgreSQL Statements](..)

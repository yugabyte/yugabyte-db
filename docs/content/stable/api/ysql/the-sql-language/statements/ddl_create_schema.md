---
title: CREATE SCHEMA statement [YSQL]
headerTitle: CREATE SCHEMA
linkTitle: CREATE SCHEMA
description: Use the CREATE SCHEMA statement to create schema in the current database.
menu:
  stable:
    identifier: ddl_create_schema
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE SCHEMA` statement to create a schema in the current database.
A schema is essentially a namespace: it contains named objects (tables, data types, functions, and operators) whose names can duplicate those of objects in other schemas.
Named objects in a schema can be accessed by using the schema name as prefix or by setting the schema name in the search path.

## Syntax

{{%ebnf%}}
  create_schema_name,
  create_schema_role,
  role_specification
{{%/ebnf%}}

Where

- `schema_name` is the name of the schema being created. If no schema_name is specified, the `role_name` is used.

- `role_name` is the role who will own the new schema. If omitted, it defaults to the user executing the command. To create a schema owned by another role, you must be a direct or indirect member of that role, or be a superuser.

- `schema_element` is a YSQL statement defining an object to be created within the schema.
Currently, only [`CREATE TABLE`](../ddl_create_table), [`CREATE VIEW`](../ddl_create_view), [`CREATE INDEX`](../ddl_create_index/), [`CREATE SEQUENCE`](../ddl_create_sequence), [`CREATE TRIGGER`](../ddl_create_trigger) and [`GRANT`](../dcl_grant) are supported as clauses within `CREATE SCHEMA`.
Other kinds of objects may be created in separate commands after the schema is created.

## Examples

- Create a schema.

```plpgsql
yugabyte=# CREATE SCHEMA IF NOT EXISTS branch;
```

- Create a schema for a user.

```plpgsql
yugabyte=# CREATE ROLE John;
yugabyte=# CREATE SCHEMA AUTHORIZATION john;
```

- Create a schema that will be owned by another role.

```plpgsql
yugabyte=# CREATE SCHEMA branch AUTHORIZATION john;
```

## See also

- [`CREATE TABLE`](../ddl_create_table)
- [`CREATE VIEW`](../ddl_create_view)
- [`CREATE INDEX`](../ddl_create_index/)
- [`CREATE SEQUENCE`](../ddl_create_sequence)
- [`GRANT`](../dcl_grant)

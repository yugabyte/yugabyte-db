---
title: CREATE SCHEMA statement [YSQL]
headerTitle: CREATE SCHEMA
linkTitle: CREATE SCHEMA
description: Use the CREATE SCHEMA statement to create schema in the current database.
menu:
  preview:
    identifier: ddl_create_schema
    parent: statements
aliases:
  - /preview/api/ysql/commands/ddl_create_schema/
type: docs
---

## Synopsis

Use the `CREATE SCHEMA` statement to create a schema in the current database.
A schema is essentially a namespace: it contains named objects (tables, data types, functions, and operators) whose names can duplicate those of objects in other schemas.
Named objects in a schema can be accessed by using the schema name as prefix or by setting the schema name in the search path.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link active" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/create_schema_name,create_schema_role,role_specification.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade show active" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/create_schema_name,create_schema_role,role_specification.diagram.md" %}}
  </div>
</div>

Where

- `schema_name` is the name of the schema being created. If no schema_name is specified, the `role_name` is used. Schema names must not begin with `pg_`. The attempt to create a schema with such a name, or to rename an existing schema to have such a name, causes an error.

- `role_name` is the role who will own the new schema. If omitted, it defaults to the user executing the command. To create a schema owned by another role, you must be a direct or indirect member of that role, or be a superuser.

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

- [`DROP SCHEMA`](../ddl_drop_schema)
- [`ALTER SCHEMA`](../ddl_alter_schema)
- [`CREATE TABLE`](../ddl_create_table)
- [`CREATE VIEW`](../ddl_create_view)
- [`CREATE INDEX`](../ddl_create_index/)
- [`CREATE SEQUENCE`](../ddl_create_sequence)
- [`GRANT`](../dcl_grant)

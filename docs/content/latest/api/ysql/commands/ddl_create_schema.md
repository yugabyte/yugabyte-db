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

Use the `CREATE SCHEMA` statement to enter a new schema into the current database. The schema name must be unique.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <i class="fas fa-file-alt" aria-hidden="true"></i>
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <i class="fas fa-project-diagram" aria-hidden="true"></i>
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
    {{% includeMarkdown "../syntax_resources/commands/create_schema.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/create_schema.diagram.md" /%}}
  </div>
</div>

## Semantics

- `AUTHORIZATION` clause is not yet supported.
- Only `CREATE TABLE`, `CREATE VIEW`, `CREATE INDEX`, `CREATE SEQUENCE`, `CREATE TRIGGER`, and `GRANT` can be used to create objects within `CREATE SCHEMA` statement. Other database objects must be created in separate commands after the schema is created.

### *schema_name*

Specify the name of the schema to be created.

### _schema_element_

Specify the SQL statement that defines a database object to be created within the schema.
Acceptable clauses are `CREATE TABLE`, `CREATE VIEW`, `CREATE INDEX`, `CREATE SEQUENCE`, and `GRANT`.

## See also

- [`CREATE TABLE`](../ddl_create_table)
- [`CREATE VIEW`](../ddl_create_view)
- [`CREATE INDEX`](../ddl_create_index)
- [`CREATE SEQUENCE`](../ddl_create_seq)
- [`GRANT`](../dcl_grant)

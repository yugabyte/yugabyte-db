---
title: CREATE DATABASE
summary: Create a new database
description: CREATE DATABASE
menu:
  latest:
    identifier: api-ysql-commands-create-db
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/ddl_create_database/
isTocNested: true
showAsideToc: true
---

## Synopsis

The `CREATE DATABASE` command creates a `database` that functions as a grouping mechanism for database objects such as [tables](../ddl_create_table).

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
    {{% includeMarkdown "../syntax_resources/commands/create_database,create_database_options.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/create_database,create_database_options.diagram.md" /%}}
  </div>
</div>

Where

- `name` is an identifier that specifies the database to be created.
- `user_name` specifies the user who will own the new database. When not specified, the database creator is the owner.
- `template` specifies name of the template from which the new database is created.
- `encoding` specifies the character set encoding to use in the new database.
- `lc_collate` specifies the collation order (LC_COLLATE).
- `lc_ctype` specifies the character classification (LC_CTYPE).
- `tablespace_name` specifies the tablespace that is associated with the database to be created.
- `allowconn` is either `true` or `false`.
- `connlimit` specifies the number of concurrent connections can be made to this database. -1 means there is no limit.
- `istemplate` is either `true` or `false`.

## Semantics

- An error is raised if YSQL database of the given `name` already exists.

- Some options in DATABASE are under development.

## See also

[`ALTER DATABASE`](../ddl_alter_db)
[`DROP DATABASE`](../ddl_drop_database)
[Other YSQL Statements](..)

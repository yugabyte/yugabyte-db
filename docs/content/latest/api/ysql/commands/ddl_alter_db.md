---
title: ALTER DATABASE
linkTitle: ALTER DATABASE
summary: Alter database
description: ALTER DATABASE
menu:
  latest:
    identifier: api-ysql-commands-alter-db
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/ddl_alter_db
isTocNested: true
showAsideToc: true
---

## Synopsis

ALTER DATABASE redefines the attributes of the specified database.

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
    {{% includeMarkdown "../syntax_resources/commands/alter_database,alter_database_option.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/alter_database,alter_database_option.diagram.md" /%}}
  </div>
</div>


where

- `name` is an identifier that specifies the database to be altered.

- tablespace_name specifies the new tablespace that is associated with the database.

- allowconn is either `true` or `false`.

- connlimit specifies the number of concurrent connections can be made to this database. -1 means there is no limit.

- istemplate is either `true` or `false`.

## Semantics

- Some options in DATABASE are under development.

## See also

[`CREATE DATABASE`](../ddl_create_database)
[`DROP DATABASE`](../ddl_drop_database)
[Other YSQL Statements](..)

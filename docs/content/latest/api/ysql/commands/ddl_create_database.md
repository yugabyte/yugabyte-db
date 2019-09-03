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

Use the `CREATE DATABASE` statement to create a database that functions as a grouping mechanism for database objects such as [tables](../ddl_create_table).

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

## Semantics

Some options in DATABASE are under development.

### _name_

Specify the name of the database to be created. An error is raised if a YSQL database of the given `name` already exists.

### _user_name_

Specify the role name of the user who will own the new database. When not specified, the database creator is the owner.

### template

Specify the name of the template from which the new database is created.

### _encoding_

Specify the character set encoding to use in the new database.

### _lc_collate_

Specify the collation order (LC_COLLATE).

### _lc_ctype_

Specify the character classification (LC_CTYPE).

### _tablespace_name_

Specify the name of the tablespace that is associated with the database to be created.

### allowconn

Either `true` or `false`.

### connlimit

Specify the number of concurrent connections can be made to this database. `-1` means there is no limit.

### istemplate

Is either `true` or `false`.

## See also

[`ALTER DATABASE`](../ddl_alter_db)
[`DROP DATABASE`](../ddl_drop_database)
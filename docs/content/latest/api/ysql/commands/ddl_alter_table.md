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

Use the `ALTER TABLE` statement to change the definition of an existing table.

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
    {{% includeMarkdown "../syntax_resources/commands/alter_table,alter_table_action,alter_table_constraint.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/alter_table,alter_table_action,alter_table_constraint.diagram.md" /%}}
  </div>
</div>

## Semantics

### *name*

Specify the name of the table. An error is raised if specified table does not exist.

### ADD COLUMN [ IF NOT EXISTS ]

Add a column.

### DROP COLUMN [IF EXISTS]

Drop a column.

### ADD *table_constraint*

Add a *table_constraint*.

### DROP *table_constraint* 

Drop a *table_constraint*.

### *table_constraint*

## See also

- [`CREATE TABLE`](../ddl_create_table)

---
title: DROP TABLE
summary: Remove a table
description: DROP TABLE
menu:
  latest:
    identifier: api-ysql-commands-drop-table
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/ddl_drop_table/
isTocNested: true
showAsideToc: true
---

## Synopsis

The `DROP TABLE` command removes a table and all of its data from the database.

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
    {{% includeMarkdown "../syntax_resources/commands/drop_table.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/drop_table.diagram.md" /%}}
  </div>
</div>


Where

- `qualified_name` is a (possibly qualified) identifier.

## Semantics

- An error is raised if the specified `table_name` does not exist.
- Associated objects to `table_name` such as prepared statements will be eventually invalidated after the drop statement is completed.

## See also

[`CREATE TABLE`](../ddl_create_table)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[Other YSQL Statements](..)

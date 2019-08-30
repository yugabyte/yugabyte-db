---
title: COPY
linkTitle: COPY
summary: COPY
description: COPY
menu:
  latest:
    identifier: api-ysql-commands-copy
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/cmd_copy
isTocNested: true
showAsideToc: true
---

## Synopsis

`COPY` command transfers data between tables and files. `COPY TO` copies from tables to files. `COPY FROM` copies from files to tables. `COPY` outputs the number of rows that were copied.

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
    {{% includeMarkdown "../syntax_resources/commands/copy_from,copy_to,copy_option.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/copy_from,copy_to,copy_option.diagram.md" /%}}
  </div>
</div>

Where

- `table_name` specifies the table to be copied.
- `column_name` specifies column to be copied.
- `query` can be either SELECT, VALUES, INSERT, UPDATE or DELETE whose results will be copied to files. RETURNING clause must be provided for INSERT, UPDATE and DELETE commands.
- `filename` specifies an absolute or relative path of a file to be copied.

## Examples

- Errors are raised if table does not exist.
- `COPY TO` can only be used with regular tables.
- `COPY FROM` can be used with either tables, foreign tables, or views.

## See also

[Other YSQL Statements](..)

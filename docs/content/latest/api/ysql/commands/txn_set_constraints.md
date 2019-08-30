---
title: SET CONSTRAINTS
linkTitle: SET CONSTRAINTS
summary: SET CONSTRAINTS
description: SET CONSTRAINTS
menu:
  latest:
    identifier: api-ysql-commands-set-constraints
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/cmd_set_constraints
isTocNested: true
showAsideToc: true
---

## Synopsis

`SET CONSTRAINTS` command checks timing for current transaction.

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
    {{% includeMarkdown "../syntax_resources/commands/set_constraints.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/set_constraints.diagram.md" /%}}
  </div>
</div>

## Semantics

- Attributes in `SET CONSTRAINTS` does not apply to `NOT NULL` and `CHECK` constraints.

## See also

[Other YSQL Statements](..)

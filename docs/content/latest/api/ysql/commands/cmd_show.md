---
title: SHOW
linkTitle: SHOW
summary: Display values of a system or session variable
description: SHOW
menu:
  latest:
    identifier: api-ysql-commands-show
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/cmd_show
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `SHOW` statement to display the value of a run-time parameter.

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
    {{% includeMarkdown "../syntax_resources/commands/show_stmt.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/show_stmt.diagram.md" /%}}
  </div>
</div>

## Semantics

- Although the values of a parameter can be set, showed, and reset, the effect of these parameters are not yet supported in YugaByte. The factory-settings or default behaviors will be used for the moment.

### name

Specify the name of the parameter to be showed.

## See also

[`SET`](../cmd_set)
[`RESET`](../cmd_reset)
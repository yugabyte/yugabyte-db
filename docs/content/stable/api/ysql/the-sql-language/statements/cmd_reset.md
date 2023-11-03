---
title: RESET statement [YSQL]
headerTitle: RESET
linkTitle: RESET
description: Use the RESET statement to restore the value of a run-time parameter to the default value.
menu:
  stable:
    identifier: cmd_reset
    parent: statements
type: docs
---

## Synopsis

Use the `RESET` statement to restore the value of a run-time parameter to the default value. `RESET` is an alternative spelling for `SET run_time_parameter TO DEFAULT`.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/reset_stmt.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade show active" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/reset_stmt.diagram.md" %}}
  </div>
</div>

## Semantics

### *run_time_parameter*

Specify the name of a mutable run-time parameter.

## See also

- [`SHOW`](../cmd_show)
- [`SET`](../cmd_set)

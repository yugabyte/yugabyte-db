---
title: SHOW statement [YSQL]
headerTitle: SHOW
linkTitle: SHOW
description: Use the SHOW statement to display the value of a run-time parameter.
menu:
  v2.12:
    identifier: cmd_show
    parent: statements
type: docs
---

## Synopsis

Use the `SHOW` statement to display the value of a run-time parameter.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/show_stmt.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/show_stmt.diagram.md" %}}
  </div>
</div>

## Semantics

- Although the values of a parameter can be set, displayed, and reset, the effect of these parameters are not yet supported in Yugabyte. The factory-settings or default behaviors will be used for the moment.

### *configuration_parameter*

Specify the name of the parameter to be displayed.

### ALL

Show the values of all configuration parameters, with descriptions.

## See also

- [`SET`](../cmd_set)
- [`RESET`](../cmd_reset)

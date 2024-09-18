---
title: RESET statement [YSQL]
headerTitle: RESET
linkTitle: RESET
description: Use the RESET statement to restore the value of a run-time parameter to the default value.
menu:
  v2.14:
    identifier: cmd_reset
    parent: statements
type: docs
---

## Synopsis

Use the `RESET` statement to restore the value of a run-time parameter to the default value. `RESET` maps to `SET configuration_parameter TO DEFAULT`.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-bs-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-bs-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/reset_stmt.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/reset_stmt.diagram.md" %}}
  </div>
</div>

## Semantics

{{< note title="Note" >}}

Although the values of a parameter can be set, displayed, and reset, the effect of these parameters are not yet supported in Yugabyte. The factory settings or default behaviors will be used for the moment.

{{< /note >}}

### *configuration_parameter*

Specify the name of a mutable run-time parameter.

## See also

- [`SHOW`](../cmd_show)
- [`SET`](../cmd_set)

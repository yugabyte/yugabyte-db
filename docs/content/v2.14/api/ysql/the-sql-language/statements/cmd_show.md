---
title: SHOW statement [YSQL]
headerTitle: SHOW
linkTitle: SHOW
description: Use the SHOW statement to display the value of a run-time parameter.
menu:
  v2.14:
    identifier: cmd_show
    parent: statements
type: docs
---

## Synopsis

Use the `SHOW` statement to display the value of a run-time parameter.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/show_stmt.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/show_stmt.diagram.md" %}}
  </div>
</div>

## Semantics

The parameter values in YSQL can be set and typically take effect the same way as in PostgreSQL. However, because YugabyteDB uses a different storage engine ([DocDB](../../../../../architecture/layered-architecture/#docdb)), many configurations related to the storage layer will not have the same effect in YugabyteDB as in PostgreSQL. For example, configurations related to connection and authentication, query planning, error reporting and logging, run-time statistics, client connection defaults, and so on, should work as in PostgreSQL.

However, configurations related to write ahead log, vacuuming, or replication, may not apply to Yugabyte. Instead related configuration can be set using yb-tserver (or yb-master) [configuration flags](../../../../../reference/configuration/yb-tserver/#configuration-flags).

### *configuration_parameter*

Specify the name of the parameter to be displayed.

### ALL

Show the values of all configuration parameters, with descriptions.

## See also

- [`SET`](../cmd_set)
- [`RESET`](../cmd_reset)

---
title: SET statement [YSQL]
headerTitle: SET
linkTitle: SET
description: Use the SET statement to update a run-time control parameter.
menu:
  v2.14:
    identifier: cmd_set
    parent: statements
type: docs
---

## Synopsis

Use the `SET` statement to update a run-time control parameter.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/set.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/set.diagram.md" %}}
  </div>
</div>

## Semantics

The parameter values that you set with this statement apply just within the scope of a single session and for no longer than the session's duration. It's also possible to set the default values for such parameters at the level of the entire cluster or at the level of a particular database. For example:

```plpgsql
alter database demo set timezone = 'America/Los_Angeles';
```

See [`ALTER DATABASE`](../ddl_alter_db/).

### SESSION

Specify that the command affects only the current session.

### LOCAL

Specify that the command affects only the current transaction. After `COMMIT` or `ROLLBACK`, the session-level setting takes effect again.

### *configuration_parameter*

Specify the name of a mutable run-time parameter.

### value

Specify the value of parameter.

## See also

- [`SHOW`](../cmd_show)
- [`RESET`](../cmd_reset)

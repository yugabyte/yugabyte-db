---
title: SET CONSTRAINTS statement [YSQL]
headerTitle: SET CONSTRAINTS
linkTitle: SET CONSTRAINTS
summary: SET CONSTRAINTS
description: Use the `SET CONSTRAINTS` statement to set the timing of constraint checking within the current transaction.
menu:
  v2.14:
    identifier: txn_set_constraints
    parent: statements
type: docs
---

## Synopsis

Use the `SET CONSTRAINTS` statement to set the timing of constraint checking within the current transaction.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/set_constraints.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/set_constraints.diagram.md" %}}
  </div>
</div>

## Semantics

Attributes in the `SET CONSTRAINTS` statement comply with the behavior defined in the SQL standard, except that it does not apply to `NOT NULL` and `CHECK` constraints.

### *set_constraints*

```
SET CONSTRAINTS { ALL | *name [ , ... ] } { DEFERRED | IMMEDIATE }
```

### ALL

Change the mode of all deferrable constraints.

### *name*

Specify one or a list of constraint names.

### DEFERRED

Set constraints to not be checked until transaction commit.

Uniqueness and exclusion constraints are checked immediately, unless marked `DEFERRABLE`.

### IMMEDIATE

Set constraints to take effect retroactively.

See also

- [`ALTER TABLE`](../ddl_alter_table)

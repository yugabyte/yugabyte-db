---
title: LOCK statement [YSQL]
headerTitle: LOCK
linkTitle: LOCK
description: Use the LOCK statement to lock a table.
menu:
  v2.16:
    identifier: txn_lock
    parent: statements
type: docs
---

## Synopsis

Use the `LOCK` statement to lock a table.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/lock_table,lockmode.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/lock_table,lockmode.diagram.md" %}}
  </div>
</div>

{{< note title="Table inheritance is not yet supported" >}}
The [table_expr](../../../syntax_resources/grammar_diagrams/#table-expr) rule specifies syntax that is useful only when at least one other table inherits one of the tables that the `truncate` statement lists explicitly. See [this note](../ddl_alter_table#table-expr-note) for more detail. Until inheritance is supported, use a bare [table_name](../../../syntax_resources/grammar_diagrams/#table-name).
{{< /note >}}

## Semantics

### *lock_table*

#### *name*

Specify a table to lock.

### *lockmode*

- Only `ACCESS SHARE` lock mode is supported at this time.
- All other modes listed in *lockmode* are under development.

```
ACCESS SHARE
  | ROW SHARE
  | ROW EXCLUSIVE
  | SHARE UPDATE EXCLUSIVE
  | SHARE
  | SHARE ROW EXCLUSIVE
  | EXCLUSIVE
  | ACCESS EXCLUSIVE
```

## See also

- [`SET TRANSACTION`](../txn_set)

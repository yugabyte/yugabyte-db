---
title: ROLLBACK statement [YSQL]
headerTitle: ROLLBACK
linkTitle: ROLLBACK
description: Use the ROLLBACK statement to roll back the current transactions.
menu:
  v2.14:
    identifier: txn_rollback
    parent: statements
type: docs
---

## Synopsis

Use the `ROLLBACK` statement to roll back the current transactions. All changes included in this transactions will be discarded.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/rollback.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/rollback.diagram.md" %}}
  </div>
</div>

## Semantics

### *rollback*

```
ROLLBACK [ TRANSACTION | WORK ]
```

### WORK

Add optional keyword — has no effect.

### TRANSACTION

Add optional keyword — has no effect.

## See also

- [`BEGIN`](../txn_begin/)
- [`COMMIT`](../txn_commit)

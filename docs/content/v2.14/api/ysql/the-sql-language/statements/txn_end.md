---
title: END statement [YSQL]
headerTitle: END
linkTitle: END
description: Use the `END` statement to commit the current transaction.
menu:
  v2.14:
    identifier: txn_end
    parent: statements
type: docs
---

## Synopsis

Use the `END` statement to commit the current transaction. All changes made by the transaction become visible to others and are guaranteed to be durable if a crash occurs.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/end.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/end.diagram.md" %}}
  </div>
</div>

## Semantics

### *end*

```
END [ TRANSACTION | WORK ]

### WORK

Add optional keyword — has no effect.

### TRANSACTION

Add optional keyword — has no effect.

## See also

- [`ABORT`](../txn_abort)
- [`BEGIN`](../txn_begin/)
- [`COMMIT`](../txn_commit)
- [`ROLLBACK`](../txn_rollback)

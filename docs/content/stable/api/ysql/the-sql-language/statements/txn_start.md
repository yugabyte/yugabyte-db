---
title: START TRANSACTION statement [YSQL]
headerTitle: START TRANSACTION
linkTitle: START TRANSACTION
description: Use the `START TRANSACTION` statement to start a transaction with the default (or specified) isolation level.
menu:
  stable:
    identifier: txn_start
    parent: statements
type: docs
---

## Synopsis

Use the `START TRANSACTION` statement to start a transaction with the default (or specified) isolation level.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/start_transaction.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade show active" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/start_transaction.diagram.md" %}}
  </div>
</div>

## Semantics

The `START TRANSACTION` statement is simply an alternative spelling for the [`BEGIN`](../txn_begin) statement. The syntax that follows `START TRANSACTION` is identical to that syntax that follows `BEGIN [ TRANSACTION | WORK ]`. And the two alternative spellings have identical semantics.

## See also

- [`ABORT`](../txn_abort)
- [`BEGIN`](../txn_begin)
- [`COMMIT`](../txn_commit)
- [`END`](../txn_end)
- [`ROLLBACK`](../txn_rollback)
- [`SET TRANSACTION`](../txn_set)

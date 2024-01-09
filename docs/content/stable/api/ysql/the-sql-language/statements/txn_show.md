---
title: SHOW TRANSACTION statement [YSQL]
headerTitle: SHOW TRANSACTION
linkTitle: SHOW TRANSACTION
description: Use the SHOW TRANSACTION ISOLATION LEVEL statement to show the current transaction isolation level.
summary: SHOW TRANSACTION
menu:
  stable:
    identifier: txn_show
    parent: statements
type: docs
---

## Synopsis

Use the `SHOW TRANSACTION ISOLATION LEVEL` statement to show the current transaction isolation level.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/show_transaction.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade show active" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/show_transaction.diagram.md" %}}
  </div>
</div>

## Semantics

Supports Serializable, Snapshot, and Read Committed Isolation<sup>$</sup> using the PostgreSQL isolation level syntax of `SERIALIZABLE`, `REPEATABLE READ`, and `READ COMMITTED` respectively. PostgreSQL's `READ UNCOMMITTED` also maps to Read Committed Isolation.

<sup>$</sup> Read Committed support is currently in [Tech Preview](/preview/releases/versioning/#feature-availability). Read Committed Isolation is supported only if the YB-TServer flag `yb_enable_read_committed_isolation` is set to `true`. By default this flag is `false` and in this case the Read Committed isolation level of YugabyteDB's transactional layer falls back to the stricter Snapshot Isolation (in which case `READ COMMITTED` and `READ UNCOMMITTED` of YSQL also in turn use Snapshot Isolation).

### TRANSACTION ISOLATION LEVEL

Show the current transaction isolation level.

The `TRANSACTION ISOLATION LEVEL` returned is either `SERIALIZABLE`, `REPEATABLE READ`, `READ COMMITTED` or `READ UNCOMMITTED`.

## See also

- [`SET TRANSACTION`](../txn_set)
- [`Transaction isolation levels`](../../../../../architecture/transactions/isolation-levels)

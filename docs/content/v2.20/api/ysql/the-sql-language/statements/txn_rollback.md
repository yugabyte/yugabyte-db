---
title: ROLLBACK statement [YSQL]
headerTitle: ROLLBACK
linkTitle: ROLLBACK
description: Use the ROLLBACK statement to roll back the current transactions.
menu:
  v2.20:
    identifier: txn_rollback
    parent: statements
type: docs
---

## Synopsis

Use the `ROLLBACK` statement to roll back the current transactions. All changes included in this transactions will be discarded.

## Syntax

{{%ebnf%}}
  rollback
{{%/ebnf%}}

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
- [`START TRANSACTION`](../txn_start/)
- [`COMMIT`](../txn_commit)

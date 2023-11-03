---
title: END statement [YSQL]
headerTitle: END
linkTitle: END
description: Use the `END` statement to commit the current transaction.
menu:
  preview:
    identifier: txn_end
    parent: statements
aliases:
  - /preview/api/ysql/commands/txn_end/
type: docs
---

## Synopsis

Use the `END` statement to commit the current transaction. All changes made by the transaction become visible to others and are guaranteed to be durable if a crash occurs.

## Syntax

{{%ebnf%}}
  end
{{%/ebnf%}}

## Semantics

### *end*

```
END [ TRANSACTION | WORK ]
```

### WORK

Add optional keyword — has no effect.

### TRANSACTION

Add optional keyword — has no effect.

## See also

- [`ABORT`](../txn_abort)
- [`BEGIN`](../txn_begin/)
- [`START TRANSACTION`](../txn_start/)
- [`COMMIT`](../txn_commit)
- [`ROLLBACK`](../txn_rollback)

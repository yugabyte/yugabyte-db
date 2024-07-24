---
title: DROP_REPLICATION_SLOT statement [YSQL]
headerTitle: DROP_REPLICATION_SLOT
linkTitle: DROP_REPLICATION_SLOT
description: Use the DROP_REPLICATION_SLOT statement to drop an existing replication slot.
menu:
  preview:
    identifier: streaming_drop_repl_slot
    parent: statements
type: docs
---

## Synopsis

Use the `DROP_REPLICATION_SLOT` statement to drop a replication slot.

## Syntax

{{%ebnf%}}
  drop_replication_slot
{{%/ebnf%}}

## Semantics

### *slot_name*

The name of the replication slot.

## Examples

- Drop a replication slot.

```sql
yugabyte=# DROP_REPLICATION_SLOT test_replication_slot;
```

## See also

- [`CREATE_REPLICATION_SLOT`](../streaming_create_repl_slot)
- [`START_REPLICATION`](../streaming_start_replication)

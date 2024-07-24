---
title: CREATE_REPLICATION_SLOT statement [YSQL]
headerTitle: CREATE_REPLICATION_SLOT
linkTitle: CREATE_REPLICATION_SLOT
description: Use the CREATE_REPLICATION_SLOT statement to create a replication slot.
menu:
  preview:
    identifier: streaming_create_repl_slot
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE_REPLICATION_SLOT` statement to create a replication slot.

## Syntax

{{%ebnf%}}
  create_replication_slot
{{%/ebnf%}}

## Semantics

### *slot_name*

The name of the replication slot. The name of the replication slot must be **unique across all databases**.

### *output_plugin*

The name of the output plugin used for logical decoding.

### NOEXPORT_SNAPSHOT / USE_SNAPSHOT

Decides what to do with the snapshot created during logical slot initialization.

`USE_SNAPSHOT` will use the snapshot for the current transaction executing the command. This option must be used in a transaction, and `CREATE_REPLICATION_SLOT` must be the first command run in that transaction.

`NOEXPORT_SNAPSHOT` will just use the snapshot for logical decoding as normal but won't do anything else with it.

## Examples

Create a Replication Slot with name *test_replication_slot* and use the `yboutput` plugin.

```sql
yugabyte=# CREATE_REPLICATION_SLOT test_replication_slot LOGICAL yboutput;
```

Create a Replication Slot with name test_replication_slot and use the pgoutput plugin.

```sql
yugabyte=# CREATE_REPLICATION_SLOT test_replication_slot LOGICAL pgoutput;
```

## See also

- [`DROP_REPLICATION_SLOT`](../streaming_drop_repl_slot)
- [`START_REPLICATION`](../streaming_start_replication)

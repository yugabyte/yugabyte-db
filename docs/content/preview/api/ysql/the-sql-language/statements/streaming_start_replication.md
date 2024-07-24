---
title: START_REPLICATION statement [YSQL]
headerTitle: START_REPLICATION
linkTitle: START_REPLICATION
description: Use the START_REPLICATION statement to start streaming from a replication slot.
menu:
  preview:
    identifier: streaming_start_replication
    parent: statements
type: docs
---

## Synopsis

Use the `START_REPLICATION` command to start streaming changes from a logical replication slot.

## Syntax

{{%ebnf%}}
  start_replication,
  log_sequence_number,
  replication_option
{{%/ebnf%}}

## Semantics

Instructs the server to start streaming WAL for logical replication. The output plugin associated with the selected slot is used to process the output for streaming.

### *slot_name*

The name of the slot to stream changes from. This parameter is required, and must correspond to an existing logical replication slot created with [`CREATE_REPLICATION_SLOT`](../streaming_create_repl_slot).

### *log_sequence_number*

The log sequence number from where to start the streaming from.

### *start_replication_option_name*

The name of an option passed to the slot's logical decoding plugin.

### *start_replication_option_value*

Optional value, in the form of a string constant, associated with the specified option.

## Example

```sql
yugabyte=# START_REPLICATION test_slot LOGICAL 0/2 publication_names 'pub';
```

## See also

- [`CREATE_REPLICATION_SLOT`](../streaming_create_repl_slot)
- [`DROP_REPLICATION_SLOT`](../streaming_drop_repl_slot)

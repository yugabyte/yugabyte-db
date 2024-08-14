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

The applicable options accepted by the command depends on the output plugin of the replication slot. They can be viewed in the respective documentation of the output plugin itself.

For `pgoutput` and `yboutput`, check the section [53.5.1. Logical Streaming Replication Parameters](https://www.postgresql.org/docs/11/protocol-logical-replication.html) in the PG documentation.

For `wal2json`, refer to the [plugin documentation](https://github.com/eulerto/wal2json/tree/master?tab=readme-ov-file#parameters).

### *start_replication_option_value*

Optional value, in the form of a string constant, associated with the specified option.

## Example

We need to follow a few steps before we can start streaming from a replication slot. Assume that a table with name `users` already exists in the database.

Create a publication `mypublication` which includes the table `users`.

```sql
yugabyte=# CREATE PUBLICATION mypublication FOR TABLE users;
```

Create a replication slot with name `test_slot` and output plugin `pgoutput`.

```sql
yugabyte=# CREATE_REPLICATION_SLOT test_slot LOGICAL pgoutput;
```

Start replication from the `test_slot` replication slot starting from LSN `0/2`. We also pass the `publication_names` parameter with value `mypublication` to the output plugin. `publication_names` is an output plugin parameter that is accepted by both `pgoutput` and `yboutput` to determine the tables to stream the data from.

```sql
yugabyte=# START_REPLICATION test_slot LOGICAL 0/2 publication_names 'mypublication';
```

## See also

- [`CREATE_REPLICATION_SLOT`](../streaming_create_repl_slot)
- [`DROP_REPLICATION_SLOT`](../streaming_drop_repl_slot)

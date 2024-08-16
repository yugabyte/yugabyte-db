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

{{< tip title="Requires replication connection." >}}
This command can only be executed on a walsender backend which can be started by establishing a replication connection. A replication connection can be created by passing the parameter `replication=database` in the connection string. Refer to examples section.
{{< /tip >}}

## Synopsis

Use the `DROP_REPLICATION_SLOT` statement to drop a replication slot.

## Syntax

{{%ebnf%}}
  drop_replication_slot
{{%/ebnf%}}

## Semantics

### *slot_name*

The name of the replication slot.

{{<note title="Note">}}
A replication slot can only be dropped after the client consuming from it has been stopped for **at least** `ysql_cdc_active_replication_slot_window_ms` duration. The default value of the flag is 5 minutes.
{{</note>}}

## Examples

Establish a replication connection to the database `yugabyte`.

```sql
bin/ysqlsh "dbname=yugabyte replication=database"
```

Drop a replication slot.

```sql
yugabyte=# DROP_REPLICATION_SLOT test_replication_slot;
```

## See also

- [`CREATE_REPLICATION_SLOT`](../streaming_create_repl_slot)
- [`START_REPLICATION`](../streaming_start_replication)

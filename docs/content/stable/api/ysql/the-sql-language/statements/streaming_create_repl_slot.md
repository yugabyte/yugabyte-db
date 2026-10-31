---
title: CREATE_REPLICATION_SLOT statement [YSQL]
headerTitle: CREATE_REPLICATION_SLOT
linkTitle: CREATE_REPLICATION_SLOT
description: Use the CREATE_REPLICATION_SLOT statement to create a replication slot.
menu:
  stable_api:
    identifier: streaming_create_repl_slot
    parent: statements
type: docs
---

{{< tip title="Requires replication connection." >}}
This command can only be executed on a walsender backend which can be started by establishing a replication connection. A replication connection can be created by passing the parameter `replication=database` in the connection string. Refer to examples section.
{{< /tip >}}

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

### EXPORT_SNAPSHOT / NOEXPORT_SNAPSHOT / USE_SNAPSHOT

Determines how the client consumes the snapshot of the database that is established when the slot is created. See [Initial snapshot](../../../../../additional-features/change-data-capture/using-logical-replication/key-concepts/#initial-snapshot) in the logical replication key concepts.

- EXPORT_SNAPSHOT (default): the command returns the name of an exported snapshot in the `snapshot_name` field. Use it with `SET TRANSACTION SNAPSHOT` in a `REPEATABLE READ` transaction on another connection to read the tables as of slot creation. As in PostgreSQL, the exported snapshot is valid until this connection runs its next command. Requires the YB-TServer flag [ysql_enable_pg_export_snapshot](../../../../../explore/ysql-language-features/advanced-features/snapshot-synchronization).
- USE_SNAPSHOT: the client reads the snapshot on this connection. This option can only be used inside a transaction, and CREATE_REPLICATION_SLOT must be the first command run in that transaction. The `snapshot_name` field contains the hybrid time of the snapshot.
- NOEXPORT_SNAPSHOT: the snapshot is used for logical decoding only. The database does not retain row history as of slot creation for tables whose replica identity does not need it, so an initial snapshot of such tables can't be taken reliably for this slot.

## Examples

Establish a replication connection to the database `yugabyte`.

```sql
bin/ysqlsh "dbname=yugabyte replication=database"
```

Create a Replication Slot with name *test_replication_slot* and use the `yboutput` plugin.

```sql
yugabyte=# CREATE_REPLICATION_SLOT test_replication_slot LOGICAL yboutput;
```

Create a Replication Slot with name test_replication_slot and use the `pgoutput` plugin.

```sql
yugabyte=# CREATE_REPLICATION_SLOT test_replication_slot LOGICAL pgoutput;
```

## See also

- [DROP_REPLICATION_SLOT](../streaming_drop_repl_slot)
- [START_REPLICATION](../streaming_start_replication)

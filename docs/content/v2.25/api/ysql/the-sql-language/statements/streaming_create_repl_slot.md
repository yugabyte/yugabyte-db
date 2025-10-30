---
title: CREATE_REPLICATION_SLOT statement [YSQL]
headerTitle: CREATE_REPLICATION_SLOT
linkTitle: CREATE_REPLICATION_SLOT
description: Use the CREATE_REPLICATION_SLOT statement to create a replication slot.
menu:
  v2.25_api:
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

### NOEXPORT_SNAPSHOT / USE_SNAPSHOT

Determines whether the snapshot will be consumed by the client during slot initialization:

- USE_SNAPSHOT: the client will consume the snapshot. This option can only be used inside a transaction, and CREATE_REPLICATION_SLOT must be the first command run in that transaction.
- NOEXPORT_SNAPSHOT: the snapshot will be used for logical decoding only.

These options are only available if the preview flag [ysql_enable_pg_export_snapshot](../../../../../explore/ysql-language-features/advanced-features/snapshot-synchronization) is set to true. When the flag is true, USE_SNAPSHOT is the default behavior. If the flag is not set, the snapshot options are not applicable and will be ignored.

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

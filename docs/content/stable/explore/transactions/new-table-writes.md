---
title: Faster writes to new tables
headerTitle: Faster writes to new tables
linkTitle: Faster writes to new tables
description: Speed up writes into tables that the same transaction created or rebuilt.
headcontent: Skip the provisional-write step when loading a table the same transaction created
tags:
  feature: early-access
  other: ysql
menu:
  stable:
    identifier: new-table-writes
    parent: explore-transactions
    weight: 310
type: docs
---

YugabyteDB speeds up statements that load data into a table the same transaction just created or rebuilt. The optimization is {{<tags/feature/ea idea="2337">}} and available in v2026.1.2 and later. It is enabled by default for statements run on their own, outside an explicit transaction block, and requires no application change.

{{<lead link="../../../architecture/transactions/skip-intents/">}}
For write-path details, concurrent-reader behavior, and compatibility, see [Skip intents optimization](../../../architecture/transactions/skip-intents/).
{{</lead>}}

## How it works

YugabyteDB normally writes rows into a provisional store first ([intents](../../../architecture/transactions/distributed-txns/#provisional-records)) and moves them into the main store when the transaction commits. That extra step exists so other sessions never see uncommitted rows.

When a statement writes into a table that the same transaction created, no other session can see that table until the transaction commits. YugabyteDB detects this and writes straight to the main store, which removes roughly half the write work and all of the commit-time cleanup for bulk operations.

Results and durability are unchanged, and the writing transaction sees its own data exactly as it would otherwise.

## Try it

{{% explore-setup-single-new %}}

`CREATE TABLE AS` creates the table and loads it in the same statement, so the optimization applies by default:

```plpgsql
CREATE TABLE sample AS
  SELECT generate_series(1, 100000) AS id;
```

To turn the optimization off for a session (for example, if other sessions may query the table during the load):

```plpgsql
SET yb_enable_new_relation_fastpath_write = off;
```

## Which operations are faster

The optimization applies to writes into a relation that the current transaction created or rebuilt, including:

- [CREATE TABLE AS](../../../api/ysql/the-sql-language/statements/ddl_create_table_as/) and `SELECT INTO`
- [COPY](../../../api/ysql/the-sql-language/statements/cmd_copy/) or [INSERT](../../../api/ysql/the-sql-language/statements/dml_insert/) into a table created earlier in the same transaction
- [ALTER TABLE](../../../api/ysql/the-sql-language/statements/ddl_alter_table/#alter-table-operations-that-involve-a-table-rewrite) operations that rebuild the table, including adding or dropping a PRIMARY KEY, changing a column's data type, and adding a column with a volatile default
- [REFRESH MATERIALIZED VIEW](../../../api/ysql/the-sql-language/statements/ddl_refresh_matview/) without `CONCURRENTLY`
- [CREATE INDEX](../../../api/ysql/the-sql-language/statements/ddl_create_index/) on a table created in the same transaction

Adding or dropping a primary key rebuilds the whole table in YugabyteDB, because rows are physically stored in primary-key order. These statements benefit more in YugabyteDB than the same statements would in PostgreSQL, where `ADD PRIMARY KEY` only builds an index.

By default this applies to statements run on their own. To use it inside `BEGIN` … `COMMIT`, see [Use it inside transaction blocks](#use-it-inside-transaction-blocks).

## Use it inside transaction blocks

{{<tags/feature/tp idea="2337">}} To use the optimization inside explicit transaction blocks, set the YB-TServer flag `ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks` to true. This also requires [Read Committed isolation](../isolation-levels/#read-committed-isolation) and [transactional DDL](../transactional-ddl/).

Because `ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks` is a preview flag, add it to the [`allowed_preview_flags_csv`](../../../reference/configuration/yb-tserver/#allowed-preview-flags-csv) list before you set it. See [Enable for transaction blocks](../../../architecture/transactions/skip-intents/#enable-for-transaction-blocks) for the full flag list and a yugabyted example.

A load then looks like this:

```plpgsql
BEGIN;
CREATE TABLE staging (id int PRIMARY KEY, payload text);
COPY staging FROM '/data/load.csv' WITH (FORMAT csv);
COMMIT;
```

With this enabled, the optimization also covers statements inside `DO` blocks, inside procedures called with `CALL`, and inside trigger bodies.

## Limitations

The following are not sped up. Statements still succeed; they use the normal write path.

- The table was not created or rebuilt in the current transaction.
- [Colocated](../../../additional-features/colocation/) tables and temporary tables.
- After a [SAVEPOINT](../../ysql-language-features/advanced-features/savepoints/), or inside a PL/pgSQL block with an `EXCEPTION` clause.
- Databases that have a publication or a CDC stream.
- `REFRESH MATERIALIZED VIEW CONCURRENTLY`.
- Statements inside a transaction block under Repeatable Read or Serializable isolation, even with the preview setting enabled. Statements run on their own still benefit at every isolation level.

If concurrent sessions may query the table while it is being loaded, turn the optimization off for that work, or see [Concurrent readers](../../../architecture/transactions/skip-intents/#concurrent-readers).

For session settings and flags, see [`yb_enable_new_relation_fastpath_write`](../../../reference/configuration/yb-tserver/#yb-enable-new-relation-fastpath-write).

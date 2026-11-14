---
title: Logical replication behavior by release
headerTitle: Behavior by release
linkTitle: Behavior by release
description: How CDC logical replication handles schema and publication changes in each YugabyteDB release, compared with PostgreSQL.
menu:
  stable:
    parent: explore-change-data-capture-logical-replication
    identifier: logical-replication-behavior-by-release
    weight: 55
type: docs
---

The following tables summarize how logical replication handles changes to streamed tables and to publications in each YugabyteDB release, and how that compares with PostgreSQL. For what happens in the current release and how to configure it, see [Advanced topics](../advanced-topic/).

## Schema and publication changes

| Change | v2024.2 to v2025.2 | v2026.1 and later | PostgreSQL |
| :--- | :--- | :--- | :--- |
| `ALTER TABLE` without a table rewrite (for example, `ADD COLUMN`, `DROP COLUMN`, `RENAME COLUMN`) | Streamed. The client receives the new schema before the first change that uses it. See [Schema evolution](../advanced-topic/#schema-evolution). | Same. | Same. |
| `ALTER TABLE ... REPLICA IDENTITY` | No effect on existing slots. A slot uses the replica identity in effect when the slot was created. See [Replica identity](../key-concepts/#replica-identity). | Same. | Takes effect immediately. |
| `ALTER PUBLICATION ... ADD TABLE` or `DROP TABLE`, or a new table in a `FOR ALL TABLES` publication | Applied at the next [periodic refresh](../advanced-topic/#periodic-publication), 15 minutes by default. Implicit publication is available as a preview flag; see [Publication change detection](#publication-change-detection). | Applied at commit time, unless the slot uses [periodic publication](../advanced-topic/#periodic-publication). See [Implicit publication](../advanced-topic/#implicit-publication). | Applied at commit time. |
| `ALTER TABLE` with a [table rewrite](../../../../api/ysql/the-sql-language/statements/ddl_alter_table/#alter-table-operations-that-involve-a-table-rewrite) (for example, `ALTER TYPE`, `ADD PRIMARY KEY`, `ADD COLUMN` with a volatile `DEFAULT`) | Blocked with an error. | Non-colocated tables: allowed, unless a slot using [periodic publication](../advanced-topic/#periodic-publication) exists on the table; existing rows are streamed again.<br>Colocated tables: blocked. See [Streaming DDLs causing table rewrite](../advanced-topic/#streaming-ddls-causing-table-rewrite). | Allowed; existing rows are not streamed again. |
| `TRUNCATE` | Blocked with an error. | Non-colocated tables: allowed, unless a slot using [periodic publication](../advanced-topic/#periodic-publication) exists on the table; no truncate record is sent.<br>Colocated tables: blocked. | Allowed; a truncate record is sent. |
| `DROP TABLE` | Not supported. The table's data is deleted immediately, and changes not yet streamed are lost. | Allowed. Changes committed before the drop are streamed, then the table leaves the stream. | Allowed. Changes committed before the drop are streamed. |

## Publication change detection

When a publication changes, a replication stream applies the change to the set of tables it polls in one of two modes:

- [Implicit publication](../advanced-topic/#implicit-publication). The change takes effect at the commit point of the `ALTER PUBLICATION` or `CREATE TABLE`.
- [Periodic publication](../advanced-topic/#periodic-publication). The change takes effect at the next scheduled refresh of the publication's table list.

Implicit is the default. To change modes, set the [ysql_yb_enable_implicit_dynamic_tables_logical_replication](../../../../reference/configuration/yb-tserver/#ysql-yb-enable-implicit-dynamic-tables-logical-replication) flag (default `true`). A slot keeps the mode it was created with.

Prior to v2026.1, the default was Periodic.

| Version | Default mode | Description |
| :--- | :--- | :--- |
| v2024.2 to v2025.2 | Periodic publication | v2024.2 only supports Periodic mode. In v2025.1 and v2025.2 `ysql_yb_enable_implicit_dynamic_tables_logical_replication` is a preview flag; to switch to Implicit mode, add the flag to the [allowed_preview_flags_csv](../../../../reference/configuration/yb-tserver/#allowed-preview-flags-csv) list and set the flag to `true` on every YB-Master and YB-TServer before creating the slot. |
| v2026.1 and later | Implicit publication, after the upgrade is finalized. | To switch to Periodic mode, set `ysql_yb_enable_implicit_dynamic_tables_logical_replication` to `false` on every YB-Master and YB-TServer and restart them. Slots created in Implicit mode stop streaming and must be dropped and recreated. |

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
| `ALTER TABLE` without a table rewrite (for example, `ADD COLUMN`, `DROP COLUMN`, `RENAME COLUMN`) | Streamed. The client receives the new schema before the first change that uses it. See [Column changes](../advanced-topic/#column-changes). | Same. | Same. |
| `ALTER TABLE ... REPLICA IDENTITY` | No effect on existing slots. A slot uses the replica identity in effect when the slot was created. See [Replica identity](../key-concepts/#replica-identity). | Same. | Takes effect immediately. |
| `ALTER PUBLICATION ... ADD TABLE` or `DROP TABLE`, or a new table in a `FOR ALL TABLES` publication | Applied at the next [periodic refresh](../advanced-topic/#periodic-publication), 15 minutes by default. | Applied at commit time. See [Implicit publication](../advanced-topic/#implicit-publication). | Applied at commit time. |
| `ALTER TABLE` with a [table rewrite](../../../../api/ysql/the-sql-language/statements/ddl_alter_table/#alter-table-operations-that-involve-a-table-rewrite) (for example, `ALTER TYPE`, `ADD PRIMARY KEY`, `ADD COLUMN` with a volatile `DEFAULT`) | Blocked with an error. | Non-colocated tables: allowed, unless a slot using [periodic publication](../advanced-topic/#slots-using-periodic-publication) exists on the table; existing rows are streamed again. Colocated tables: blocked. See [Streaming DDLs causing table rewrite](../advanced-topic/#streaming-ddls-causing-table-rewrite). | Allowed; existing rows are not streamed again. |
| `TRUNCATE` | Blocked with an error. | Non-colocated tables: allowed, unless a slot using [periodic publication](../advanced-topic/#slots-using-periodic-publication) exists on the table; no truncate record is sent. Colocated tables: blocked. | Allowed; a truncate record is sent. |
| `DROP TABLE` | Not supported. The table's data is deleted immediately, and changes not yet streamed are lost. | Allowed. Changes committed before the drop are streamed, then the table leaves the stream. | Allowed. Changes committed before the drop are streamed. |

## Publication change detection

When a publication changes, a replication stream applies the change to the set of tables it polls in one of two modes:

- **Immediate effect**, called [implicit publication](../advanced-topic/#implicit-publication). The change takes effect at the commit point of the `ALTER PUBLICATION` or `CREATE TABLE`.
- **Refresh interval**, called [periodic publication](../advanced-topic/#periodic-publication). The change takes effect at the next scheduled refresh of the publication's table list.

The mode is fixed for each replication slot when the slot is created. See [Slots using periodic publication](../advanced-topic/#slots-using-periodic-publication) for the consequences.

| Version | Default mode | How to select the other mode |
| :--- | :--- | :--- |
| v2024.2 to v2025.2 | Periodic publication | Not available. |
| v2026.1 and later | Implicit publication, after the upgrade is finalized. | Set [ysql_yb_enable_implicit_dynamic_tables_logical_replication](../../../../reference/configuration/yb-tserver/#ysql-yb-enable-implicit-dynamic-tables-logical-replication) to `false` on every YB-Master and YB-TServer and restart them. |

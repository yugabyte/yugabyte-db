---
title: Replication slot behavior by version
headerTitle: Slot behavior by version
linkTitle: Slot behavior by version
description: How YugabyteDB replication slot behavior has changed across releases and how it compares with PostgreSQL.
headcontent: Slot scope, retention, restart time, and expiry across releases
menu:
  stable:
    parent: explore-change-data-capture-logical-replication
    identifier: slot-behavior-by-version
    weight: 55
type: docs
---

The following table summarizes how [replication slot](../key-concepts/#replication-slot) behavior has changed across releases and how it compares with PostgreSQL 15, which YugabyteDB logical replication is based on. For the behavior itself, see [Key concepts](../key-concepts/#replication-slot).

| Behavior | v2024.2 to v2025.2 | v2026.1 and later | PostgreSQL 15 |
| :--- | :--- | :--- | :--- |
| Tables retained for a slot | All tables in the database, from slot creation. Tables created later are added automatically. | Same. | The slot retains WAL, which is shared by all tables and databases. |
| Streaming without a publication | All tables, for `test_decoding` and `wal2json`. `pgoutput` and `yboutput` require a publication. | Same. | Same. |
| Publication changes while streaming | Applied at the next [periodic refresh](../advanced-topic/#periodic-publication), 15 minutes by default. | [Implicit publication](../advanced-topic/#implicit-publication) applies changes at commit time. | Applied at commit time. |
| Tables not of interest | A table not polled within 4 hours of slot creation becomes unqualified. Its changes are no longer retained for the slot, even if it is added to the publication later. From v2025.2.7.0, `cdc_skip_unqualified_tables_for_polling` lets the slot skip such tables instead of failing. | Same. The flag is available from v2026.1.2.0. | No equivalent. A slot retains WAL regardless of which tables are published. |
| When the restart time advances | On acknowledgment, to the latest fully acknowledged commit. Every 60 seconds while idle, if everything sent has been acknowledged. | Same. | On acknowledgment, `restart_lsn` moves to the oldest WAL still needed to decode unacknowledged transactions. |
| Retention while the client is disconnected | WAL, intents, and row history from the restart time, for up to 8 hours since the last poll (`cdc_intent_retention_ms`, `cdc_wal_retention_time_secs`). | Same. | WAL from `restart_lsn`, without a time limit. `max_slot_wal_keep_size` (off by default) caps it by size. |
| Resuming after reconnection | From the restart time. Transactions after it are re-sent in full with the same LSNs. | Same. | From `restart_lsn`. Transactions after `confirmed_flush_lsn` are re-sent in full. |
| When a slot becomes unusable | After 8 hours without a poll (expired), or when the publication contains an unqualified table. The slot must be dropped and recreated. | Same. | When WAL beyond `max_slot_wal_keep_size` is removed. The slot must be dropped and recreated. |
| Slot state in `pg_replication_slots` | `active` is true if the slot was consumed in the last 60 seconds. `yb_stream_id` and `yb_restart_commit_ht` columns. From v2025.1: `yb_lsn_type` and `yb_restart_time` columns, and `wal_status` is `lost` when no tablet of the slot was polled for 8 hours. | Same. | `active` is true while a process holds the slot. `wal_status` is `lost` for an invalidated slot. |

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

| Behavior | YugabyteDB | PostgreSQL 15 |
| :--- | :--- | :--- |
| Tables retained for a slot | **All versions:** All tables in the database, from slot creation. Tables created later are added automatically. | WAL, shared by all tables and databases. |
| Streaming without a publication | **All versions:** All tables, for `test_decoding` and `wal2json`. `pgoutput` and `yboutput` require a publication. | Same. |
| Publication changes while streaming | **v2024.2 to v2025.2:** Applied at the next [periodic refresh](../advanced-topic/#periodic-publication), every 15 minutes by default.<br>**v2026.1 and later:** Applied at commit time with [implicit publication](../advanced-topic/#implicit-publication). | Applied at commit time. |
| Tables not of interest | **All versions:** A table not polled within 4 hours of slot creation becomes unqualified. Its changes are no longer retained for the slot, even if it is added to the publication later. The `cdc_skip_unqualified_tables_for_polling` flag lets the slot skip such tables instead of failing.<br>**v2024.2 to v2025.2:** Flag available from v2025.2.7.0.<br>**v2026.1 and later:** Flag available from v2026.1.2.0. | No equivalent. A slot retains WAL regardless of which tables are published. |
| When the restart time advances | **All versions:** On acknowledgment, to the latest fully acknowledged commit. Every 60 seconds while idle, if everything sent has been acknowledged. | On acknowledgment, `restart_lsn` moves to the oldest WAL still needed to decode unacknowledged transactions. |
| Retention while the client is disconnected | **All versions:** WAL, intents, and row history from the restart time, for up to 8 hours since the last poll (`cdc_intent_retention_ms`, `cdc_wal_retention_time_secs`). | WAL from `restart_lsn`, without a time limit. `max_slot_wal_keep_size` (off by default) caps it by size. |
| Resuming after reconnection | **All versions:** From the restart time. Transactions after it are re-sent in full with the same LSNs. | From `restart_lsn`. Transactions after `confirmed_flush_lsn` are re-sent in full. |
| When a slot becomes unusable | **All versions:** After 8 hours without a poll (expired), or when the publication contains an unqualified table (unless `cdc_skip_unqualified_tables_for_polling` is set). The slot must be dropped and recreated. | When WAL beyond `max_slot_wal_keep_size` is removed. The slot must be dropped and recreated. |
| Slot state in `pg_replication_slots` | **All versions:** `active` is true if the slot was consumed in the last 60 seconds. Includes `yb_stream_id` and `yb_restart_commit_ht`. From v2025.1, also `yb_lsn_type` and `yb_restart_time`, and `wal_status` is `lost` when no tablet of the slot was polled for 8 hours. | `active` is true while a process holds the slot. `wal_status` is `lost` for an invalidated slot. |

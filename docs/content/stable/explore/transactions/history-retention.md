---
title: History retention for long-running transactions
headerTitle: History retention for long-running transactions
linkTitle: History retention
description: Keep the snapshot of a long-running YSQL transaction or DDL statement readable by pinning history retention for its database.
headcontent: Avoid Snapshot too old errors in long-running statements and transactions
menu:
  stable:
    identifier: history-retention
    parent: explore-transactions
    weight: 320
type: docs
---

Available in v2026.2.0 and later.

YugabyteDB uses multi-version concurrency control (MVCC). Each update or delete writes a new version of a row and keeps the old one, so that a transaction can read the database as of its snapshot (its read time). Compactions remove old versions to save disk space. A version can be removed once it is older than the history retention cutoff.

By default, the cutoff trails the current time by [timestamp_history_retention_interval_sec](../../../reference/configuration/yb-tserver/#timestamp-history-retention-interval-sec) (900 seconds). If a statement or transaction reads at a snapshot older than the cutoff, it fails with a `Snapshot too old` error, such as the following:

```output
ERROR:  Snapshot too old: Snapshot too old. Read point: { physical: 1628678717824559 }, earliest read time allowed: { physical: 1628679675271006 }, delta (usec): 957446447 ...
```

A long-running YSQL statement or transaction can instead _pin_ its snapshot. A pin holds back the history retention cutoff for its database until the statement or transaction finishes, or until the pin reaches a hard cap (24 hours by default). This lets a long DDL statement, such as `CREATE TABLE AS` or `REFRESH MATERIALIZED VIEW`, run longer than the retention interval without raising that interval for the whole universe.

## How pinning works

1. Each YSQL backend tracks the read time of its oldest live snapshot. If the [pin mode](#choose-what-pins) allows it, the backend publishes that read time as its pin.
1. Each YB-TServer finds the oldest pin per database among its own backends and sends it to the YB-Master leader in its heartbeat. A pin is sent only after the transaction has been alive for [db_history_retention_pin_min_txn_age_sec](../../../reference/configuration/yb-tserver/#db-history-retention-pin-min-txn-age-sec) (300 seconds by default). Younger transactions are already protected by the retention interval.
1. The YB-Master leader computes the oldest pin per database across the universe and sends it back to every YB-TServer.
1. During compaction, a YB-TServer keeps all history of a tablet that is newer than the pin of the tablet's database.

Pinning has the following properties:

- **Pins only add retention.** History newer than `timestamp_history_retention_interval_sec` is always kept, with or without a pin.
- **Scope is the database.** A pin holds back history on every tablet of its database, including tablets that the transaction does not read. Other databases are not affected.
- **The system catalog is also protected.** The YB-Master keeps the YSQL system catalog history back to the oldest pin. Because all databases share the system catalog, the oldest pin in _any_ database holds back catalog history for all databases. The catalog's own floor, [timestamp_syscatalog_history_retention_interval_sec](../../../reference/configuration/yb-tserver/#timestamp-syscatalog-history-retention-interval-sec) (4 hours), still applies.
- **Pins are capped.** A pin older than [db_history_retention_pin_max_txn_age_sec](../../../reference/configuration/yb-tserver/#db-history-retention-pin-max-txn-age-sec) (24 hours by default) no longer holds back compaction. The transaction that holds it fails with `Snapshot too old` on its next read of compacted data.
- **Pins survive a YB-TServer restart.** Each YB-TServer saves the universe-wide pins to local disk, and applies them at startup before its first heartbeat.
- **Mixed-version universes do not pin.** While a universe is being upgraded from a version without pinning, history retention uses only the retention interval.

## Choose what pins

The YSQL configuration parameter [yb_db_history_retention_pin_mode](../../../reference/configuration/yb-tserver/#yb-db-history-retention-pin-mode) controls which statements and transactions publish a pin. It can take the following values:

| Value | Behavior |
| :---- | :------- |
| `ddl_only` (default) | A pin is published only while a DDL statement runs. |
| `all` | Both DML and DDL publish a pin. In REPEATABLE READ transactions, the transaction snapshot is pinned for the life of the transaction. In READ COMMITTED transactions, each statement's snapshot is pinned while the statement runs. An open cursor keeps its snapshot pinned until it is closed. |
| `none` | No pins are published. History retention uses only the retention interval, as in earlier versions. |

You can set the parameter for a session, a role, a database, or the whole universe. For example, to pin the snapshot of a long report in one session:

```sql
SET yb_db_history_retention_pin_mode = 'all';
BEGIN ISOLATION LEVEL REPEATABLE READ;
-- long-running reads
COMMIT;
```

To pin all transactions of a reporting role:

```sql
ALTER ROLE reporting SET yb_db_history_retention_pin_mode = 'all';
```

To change the default for the whole universe, set `--ysql_pg_conf_csv=yb_db_history_retention_pin_mode=all` on all YB-TServers.

To turn pinning off for the whole universe, set [enable_db_history_retention_pins](../../../reference/configuration/yb-tserver/#enable-db-history-retention-pins) to `false` on all YB-Masters and YB-TServers. The flag can be changed at runtime.

## Cost of pinning

While a pin is held, its database keeps every row version written after the pin's read time. On a database with many updates and deletes, this has the following costs:

- **Disk space.** Old row versions and delete markers stay on disk until the pin is released and the next compaction runs.
- **Read latency.** Reads must skip more old versions, so scans over frequently updated rows get slower.

These costs apply to the whole database, not just to the tables that the pinned transaction reads. In `all` mode, a single forgotten session that is idle in a transaction can hold back history for its database for up to `db_history_retention_pin_max_txn_age_sec`. To limit this, do one or more of the following:

- Use `all` mode only for the sessions or roles that need it.
- Set [idle_in_transaction_session_timeout](../../../develop/learn/transactions/transactions-performance-ysql/#handle-idle-applications) so that idle transactions are ended.
- Lower `db_history_retention_pin_max_txn_age_sec` to a bit more than the longest statement or transaction that needs a pin. Don't set it below `timestamp_history_retention_interval_sec`: the hard cap applies to the retention interval too, so a lower value shortens history retention for every YSQL database.

## Find and end a session that holds a pin

Pinning has no metrics yet. Instead, at most every [db_history_retention_pin_log_interval_sec](../../../reference/configuration/yb-tserver/#db-history-retention-pin-log-interval-sec) (60 seconds by default), each YB-TServer writes one line per pinned database to its log. The line names the database OID, the age of the oldest pin, and the process ID of the backend that holds it, as in the following example:

```output
DB: 16384 retaining for: 7740.000s due to pid: 67890
```

To find the database and the session, connect to the node that wrote the line and run the following:

```sql
SELECT datname FROM pg_database WHERE oid = 16384;

SELECT pid, usename, state, xact_start, query
FROM pg_stat_activity
WHERE pid = 67890;
```

If the session should not hold the pin, end it:

```sql
SELECT pg_terminate_backend(67890);
```

When the hard cap is reached, the YB-TServer logs a warning similar to the following, and compaction removes history that the pin covered:

```output
Compacting past the history retention pin of database yugabyte (oid 16384): pin ..., held for 86410.512s, is older than db_history_retention_pin_max_txn_age_sec (86400s). Transactions reading at this time will fail with snapshot too old on their next read. Tablet: ...
```

## Limitations

- **Some DDL is not yet protected in `ddl_only` mode.** An ALTER TABLE that rewrites the table and a nonconcurrent CREATE INDEX take their snapshot before the statement starts pinning, so they can still fail with `Snapshot too old` after `timestamp_history_retention_interval_sec`. See issue {{<issue 35581>}}. For a nonconcurrent CREATE INDEX, follow the guidance in [CREATE INDEX](../../../api/ysql/the-sql-language/statements/ddl_create_index/#concurrent-index-creation).
- **Index backfill is not covered.** The backfill phase of a concurrent CREATE INDEX does not use a pin. It still requires `timestamp_history_retention_interval_sec` to be larger than the backfill time.
- **Scope is the whole database.** A pin can't be limited to the tables that the transaction reads.
- **YSQL only.** YCQL statements do not pin.

## Configuration reference

| Name | Type | Default | Description |
| :--- | :--- | :------ | :---------- |
| [yb_db_history_retention_pin_mode](../../../reference/configuration/yb-tserver/#yb-db-history-retention-pin-mode) | YSQL parameter | `ddl_only` | Which statements and transactions publish a pin. |
| [enable_db_history_retention_pins](../../../reference/configuration/yb-tserver/#enable-db-history-retention-pins) | YB-Master and YB-TServer flag | `true` | Turns pinning on or off for the universe. When `false`, no pins are reported or applied. |
| [db_history_retention_pin_min_txn_age_sec](../../../reference/configuration/yb-tserver/#db-history-retention-pin-min-txn-age-sec) | YB-TServer flag | `300` | How long a transaction must be alive before its pin is reported. Must be less than `timestamp_history_retention_interval_sec`. |
| [db_history_retention_pin_max_txn_age_sec](../../../reference/configuration/yb-tserver/#db-history-retention-pin-max-txn-age-sec) | YB-Master and YB-TServer flag | `86400` | The hard cap. History older than this is compacted even if a pin covers it. |
| [db_history_retention_pin_log_interval_sec](../../../reference/configuration/yb-tserver/#db-history-retention-pin-log-interval-sec) | YB-TServer flag | `60` | How often the per-database pin log line is written. `0` turns the log line off. |
| [history_retention_pins_persist_interval_sec](../../../reference/configuration/yb-tserver/#history-retention-pins-persist-interval-sec) | YB-TServer flag | `60` | How often the universe-wide pins are saved to local disk. |

All the flags can be changed at runtime.

---
title: CDC monitoring in YugabyteDB
headerTitle: Monitor
linkTitle: Monitor
description: Monitor Change Data Capture in YugabyteDB.
menu:
  preview:
    parent: explore-change-data-capture-logical-replication
    identifier: monitor
    weight: 30
type: docs
---

## Catalog Objects & Views

### pg_publication

Contains all publication objects contained in the database.

| Column name  | Data type | Description |
| :----- | :----- | :----- |
| oid | oid | Row identifier |
| pubname | name | Name of the publication |
| pubowner | oid | OID of the owner. |
| puballtables | bool | If true, this publication includes all tables in the database including those added in the future. |
| pubinsert | bool | If true, INSERT operations are replicated for tables in the publication. |
| pubupdate | bool | If true, UPDATE operations are replicated for tables in the publication. |
| pubdelete | bool | If true, DELETE operations are replicated for tables in the publication. |
| pubtruncate | bool | If true, TRUNCATE operations are replicated for tables in the publication. |


### pg_publication_rel

Contains mapping between publications and tables. This is a many to many mapping.

| Column name | Data type | Description |
| :----- | :----- | :----- |
| oid | oid | Row identifier |
| prpubid | oid | OID of the publication. References pg_publication.oid |
| prrelid| oid  | OID of the relation. References pg_class.oid |


### pg_publication_tables

Contains mapping between publications and tables. It is a wrapper over `pg_publication_rel` as it expands the publications defined as FOR ALL TABLES, so for such publications there will be a row for each eligible table.

| Column name | Data type | Description |
| :----- | :----- | :----- |
| pubname | name | Name of publication |
| schemaname | name | Name of schema containing table |
| tablename | name | Name of table |


### pg_replication_slots

Provides a list of all replication slots that currently exist on the database cluster, along with their metadata.

| Column name | Data type | Description |
| :----- | :----- | :----- |
| slot_name | name | Name of the replication slot |
| plugin | name | Output plugin name (Always `yboutput`) |
| slot_type | text | Always logical |
| datoid | oid | The OID of the database this slot is associated with. |
| database | text | The name of the database this slot is associated with. |
| temporary | boolean | True if this is a temporary replication slot. Temporary slots are automatically dropped on error or when the session has finished. |
| active | boolean | True if this slot is currently actively being used. In YSQL, an "active" replication slot means a slot which has been consumed at least once in a certain timeframe. We will define this timeframe via a GFlag `ysql_replication_slot_activity_threshold` with a default value of 5 minutes. |
| active_pid | integer | The process ID of the session using this slot if the slot is currently actively being used. `NULL` if no replication process is ongoing. |
| xmin | xid | The oldest transaction that this slot needs the database to retain. |
| catalog_xmin | xid | Not applicable for YSQL. Always set to xmin. |
| restart_lsn | pg_lsn | The LSN of the oldest change record which still might be required by the consumer of this slot and thus won't be automatically removed during checkpoints. |
| confirmed_flush_lsn | pg_lsn | The LSN up to which the logical slot's consumer has confirmed receiving data. Data older than this is not available anymore. Transactions with commit LSN lower than the confirmed_flush_lsn are not available anymore. |
| yb_stream_id | text | UUID of the CDC stream |
| yb_restart_commit_ht | int8 | A uint64 representation of the commit Hybrid Time corresponding to the restart_lsn. This can be used by the client (like YugabyteDB connector) to perform a consistent snapshot (as of the consistent_point) in the case when a replication slot already exists. |   

## CDC Service metrics

Provide information about CDC service in YugabyteDB.

| Metric name | Type | Description |
| :---- | :---- | :---- |
| cdcsdk_change_event_count | `long` | The Change Event Count metric shows the number of records sent by the CDC Service. |
| cdcsdk_traffic_sent | `long` | The number of milliseconds since the connector has read and processed the most recent event. |
| cdcsdk_event_lag_micros | `long` | The LAG metric is calculated by subtracting the timestamp of the latest record in the WAL of a tablet from the last record sent to the CDC connector. |
| cdcsdk_expiry_time_ms | `long` | The time left to read records from WAL is tracked by the Stream Expiry Time (ms). |

## Connector metrics

<!-- TODO (Siddharth): Fix link to connector metrics section -->

Refer to the [monitoring section](../yugabytedb-connector#monitoring) of YugabyteDB connector for connector metrics.
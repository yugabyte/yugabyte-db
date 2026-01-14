---
title: Monitor CDC (logical replication) in YugabyteDB
headerTitle: Monitor
linkTitle: Monitor
description: Monitor CDC with logical replication in YugabyteDB.
menu:
  v2025.1:
    parent: explore-change-data-capture-logical-replication
    identifier: monitor
    weight: 30
type: docs
---

## Catalog objects and views

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

Contains mapping between publications and tables. This is a many-to-many mapping.

| Column name | Data type | Description |
| :----- | :----- | :----- |
| oid | oid | Row identifier. |
| prpubid | oid | OID of the publication. References pg_publication.oid. |
| prrelid| oid  | OID of the relation. References pg_class.oid. |

### pg_publication_tables

Contains mapping between publications and tables. It is a wrapper over `pg_publication_rel` as it expands the publications defined as FOR ALL TABLES, so for such publications there will be a row for each eligible table.

| Column name | Data type | Description |
| :----- | :----- | :----- |
| pubname | name | Name of publication. |
| schemaname | name | Name of schema containing table. |
| tablename | name | Name of table. |

### pg_replication_slots

Provides a list of all replication slots that currently exist on the database cluster, along with their metadata.

| Column name | Data type | Description |
| :----- | :----- | :----- |
| slot_name | name | Name of the replication slot. |
| plugin | name | Output plugin name. |
| slot_type | text | Always logical. |
| datoid | oid | The OID of the database this slot is associated with. |
| database | text | The name of the database this slot is associated with. |
| temporary | boolean | True if this is a temporary replication slot. Temporary slots are automatically dropped on error or when the session has finished. |
| active | boolean | True if this slot is currently actively being used. In YSQL, an "active" replication slot means a slot which has been consumed at least once in a certain time frame. The time is defined using the `ysql_cdc_active_replication_slot_window_ms` flag, which has a default of 5 minutes. |
| active_pid | integer | The process ID of the session using this slot if the slot is currently actively being used. `NULL` if no replication process is ongoing. |
| xmin | xid | The oldest transaction that this slot needs the database to retain. |
| catalog_xmin | xid | Not applicable for YSQL. Always set to xmin. |
| restart_lsn | pg_lsn | The Log Sequence Number ([LSN](../key-concepts/#lsn-type)) of the oldest change record which still might be required by the consumer of this slot and thus won't be automatically removed during checkpoints. |
| confirmed_flush_lsn | pg_lsn | The LSN up to which the logical slot's consumer has confirmed receiving data. Data older than this is not available anymore. Transactions with commit LSN lower than the `confirmed_flush_lsn` are not available anymore. |
| yb_stream_id | text | UUID of the CDC stream |
| yb_restart_commit_ht | int8 | A uint64 representation of the commit Hybrid Time corresponding to the `restart_lsn`. This can be used by the client (like YugabyteDB connector) to perform a consistent snapshot (as of the `consistent_point`) in the case when a replication slot already exists. |

### pg_stat_replication

Displays information about active WAL senders, providing insights into the state of replication for each connected standby or logical replication client.

| Column name | Data type | Description |
| :----- | :----- | :----- |
| pid | integer | Process ID of WAL sender process. |
| usesysid | oid | OID of the user logged into this WAL sender process. |
| usename | name | Name of the user logged into this WAL sender process. |
| application_name | text | Name of the application that is connected to this WAL sender. |
| client_addr | inet | IP address of the client connected to this WAL sender. If this field is null, it indicates that the client is connected via a Unix socket on the server machine. |
| client_hostname | text | Host name of the connected client, as reported by a reverse DNS lookup of client_addr. This field will only be non-null for IP connections, and only when the [log_hostname](https://www.postgresql.org/docs/15/runtime-config-logging.html#GUC-LOG-HOSTNAME) configuration parameter is enabled. |
| client_port | integer | TCP port number that the client is using for communication with this WAL sender, or -1 if a Unix socket is used. |
| backend_start | timestamp with time zone | Time when this process was started (that is, when the client connected to this WAL sender). |
| backend_xmin | xid | The oldest transaction the client is interested in. |
| state | text | Current WAL sender state. Always `streaming`. |
| sent_lsn | pg_lsn | Last write-ahead log location sent on this connection. |
| write_lsn | pg_lsn | The last LSN acknowledged by the logical replication client. |
| flush_lsn | pg_lsn | Same as `write_lsn`. |
| replay_lsn | pg_lsn | Same as `write_lsn`. |
| write_lag | interval | The difference between the timestamp of the latest record in WAL and the timestamp of the last acknowledged record. Since YugabyteDB does not differentiate between write, flush, or replay, this value is the same for all three lag metrics. |
| flush_lag | interval | Same as `write_lag`. |
| replay_lag | interval | Same as `write_lag`. |
| sync_priority | integer | Synchronous state of this standby server. Always 0, as logical replication only supports asynchronous replication. |
| sync_state | text | Synchronous state of this standby server. Always `async`. |
| reply_time | timestamp with time zone | Timestamp of the last reply message received from the client. |

## CDC service metrics

Provide information about the CDC service in YugabyteDB.

| Metric name | Type | Description |
| :---- | :---- | :---- |
| cdcsdk_change_event_count | `long` | The number of records sent by the CDC Service. |
| cdcsdk_traffic_sent | `long` | Total traffic sent, in bytes. |
| cdcsdk_sent_lag_micros | `long` | This lag metric is calculated by subtracting the timestamp of the latest record in the WAL of a tablet from the last record sent to the CDC connector. |
| cdcsdk_expiry_time_ms | `long` | The time left to read records from WAL is tracked by the Stream Expiry Time (ms). |
| cdcsdk_flush_lag | `long` | This lag metric shows the difference between the timestamp of the latest record in the WAL and the replication slot's restart time.|

CDC service metrics are only calculated for tablets that are of interest for a replication slot. By default, tablets are considered to be of interest if they are polled at least once in 4 hours. You can configure the frequency using the [cdcsdk_tablet_not_of_interest_timeout_secs](../../../../reference/configuration/yb-tserver/#cdcsdk-tablet-not-of-interest-timeout-secs) YB-TServer flag. Metrics are calculated considering unpolled tablets until this timeout elapses.

## Connector metrics

<!-- TODO (Siddharth): Fix link to connector metrics section -->

Refer to [Monitoring](../yugabytedb-connector/#monitoring) for information on YugabyteDB connector metrics.

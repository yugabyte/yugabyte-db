---
title: Tablet metadata
headerTitle: Tablet metadata
linkTitle: Tablet metadata
description: Columns of yb_local_tablets and yb_tablet_metadata.
headcontent: Look up local and cluster-wide tablet placement and leadership
menu:
  v2025.2:
    identifier: tablet-metadata
    parent: monitor-and-alert
    weight: 122
type: docs
---

## yb_local_tablets

The `yb_local_tablets` view returns metadata for [YSQL](../../../api/ysql/), [YCQL](../../../api/ycql/), and system [tablets](../../../architecture/key-concepts/#tablet) on the node you are connected to. It shows the same information as `<yb-tserver-ip>:9000/tablets`.

When you join this view to [Active Session History](../active-session-history-monitor/#yb-active-session-history), `wait_event_aux` stores only the first 15 characters of the tablet ID. Join on `SUBSTRING(tablet_id, 1, 15)`.

{{<lead link="../../../explore/observability/yb-local-tablets/">}}
For queries against this view, see [yb_local_tablets](../../../explore/observability/yb-local-tablets/).
{{</lead>}}

### Columns

| Column | Type | Description |
| :----- | :--- | :---------- |
| tablet_id | text | 16 byte UUID of the tablet. |
| table_id | text | 16 byte UUID of the table which the tablet is part of. |
| table_type | text | Type of the table. Can be YSQL, YCQL, System, or Unknown. |
| namespace_name | text | Name of the database or the keyspace. |
| ysql_schema_name | text | YSQL schema name. Empty for YCQL, System, and Unknown table types. |
| table_name | text | Name of the table which the tablet is part of. |
| partition_key_start | bytea | Start key of the partition (inclusive). |
| partition_key_end | bytea | End key of the partition (exclusive). |
| state | text | State of the tablet. See [Tablet states](#tablet-states). |

### Tablet states

The `state` column can have the following values:

| State | Description |
| :---- | :---------- |
| TABLET_DATA_COPYING | The tablet is doing remote bootstrap. |
| TABLET_DATA_READY | Fresh empty tablets or successfully remote bootstrapped tablets. |
| TABLET_DATA_DELETED | The tablet replica has been deleted from this particular node because the tablet has been deleted from the table. |
| TABLET_DATA_TOMBSTONED | The tablet replica has been deleted from this particular node but the tablet itself is still part of the table and might have data on other nodes. |
| TABLET_DATA_SPLIT_COMPLETED | The tablet split has been completed. |
| TABLET_DATA_INIT_STARTED | The tablet has been initialized as a subtablet of another tablet undergoing a split. |

## yb_tablet_metadata

{{<tags/feature/ea idea="2358">}}The `yb_tablet_metadata` view returns tablet placement and replica roles for the whole cluster. `yb_local_tablets` is limited to the node you are connected to. `yb_tablet_metadata` is the YSQL equivalent of the YCQL `system.partitions` table.

The view returns tablet information for YSQL objects and the system transaction table only. Use it to find every tablet of a table, the leader of a tablet, and the tablet that holds a row in a [hash-sharded](../../../architecture/docdb-sharding/sharding/#hash-sharding) table.

{{<lead link="../../../explore/observability/yb-tablet-metadata/">}}
For queries against this view, see [yb_tablet_metadata](../../../explore/observability/yb-tablet-metadata/).
{{</lead>}}

### Columns

| Column | Type | Description |
| :----- | :--- | :---------- |
| tablet_id | text | A unique identifier (UUID) representing the tablet. |
| oid | oid | The object identifier (OID) for the table or index that the tablet belongs to. |
| db_name | text | Name of the database this relation belongs to. |
| relname | text | Name of the table or index whose data is stored on the tablet. |
| start_hash_code | int | Starting hash code (inclusive) for the tablet. NULL for range-sharded tables. |
| end_hash_code | int | Ending hash code (exclusive) for the tablet. NULL for range-sharded tables. |
| leader | text | IP address and port of the leader node for the tablet. |
| replicas | text[] | Replica IP addresses and ports for the tablet, including the leader. |

---
title: Tablet metadata
headerTitle: Tablet metadata
linkTitle: Tablet metadata
description: Columns of the yb_local_tablets view.
headcontent: Look up tablets on the node you are connected to
tags:
  feature: tech-preview
menu:
  v2024.2:
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

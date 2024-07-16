---
title: View metadata for YSQL/YCQL/system tablets on a server
linkTitle: Tablets metadata
description: View metadata for YSQL, YCQL, and system tablets on a server.
headerTitle: View YSQL/YCQL and tablet metadata with yb_local_tablets
menu:
  stable:
    identifier: yb-local-tablets
    parent: explore-observability
    weight: 320
type: docs
---

Use YSQL `yb_local_tablets` view to fetch the metadata for [YSQL](../../../api/ysql/), [YCQL](../../../api/ycql/), and system [tablets](../../../architecture/key-concepts/#tablet) of a node. This view returns the same information that is available on `<yb-tserver-ip>:9000/tablets`.

While debugging a system with [Active Session History](../../observability/active-session-history/), the `tablet_id` column of this view can be can be joined with the `wait_event_aux` column of the [yb_active_session_history](../../observability/active-session-history/#yb-active-session-history) view. For example, see [Detect a hot shard](../../observability/active-session-history/#detect-a-hot-shard).

The columns of the `yb_local_tablets` view are described in the following table.

| Column | Type | Description |
| :----- | :--- | :---------- |
| tablet_id | text | 16 byte UUID of the tablet. |
| table_id | text | 16 byte UUID of the table which the tablet is part of. |
| table_type | text | Type of the table. Can be YSQL, YCQL, System, or Unknown. |
| namespace_name | text | Name of the database or the keyspace. |
| ysql_schema_name | text | YSQL schema name. Empty for YCQL, System, and Unknown table types. |
| table_name | text | Name of the table which the tablet is part of. |
| partition_key_start| bytea | Start key of the partition (inclusive). |
| partition_key_end  | bytea | End key of the partition (exclusive).|

## Examples

{{% explore-setup-single %}}

Note that as this view is accessible via YSQL, run your examples using [ysqlsh](../../../admin/ysqlsh/#starting-ysqlsh).

### Describe the columns in the view

```sql
yugabyte=# \d yb_local_tablets
```

```output
              View "pg_catalog.yb_local_tablets"
       Column        | Type  | Collation | Nullable | Default
---------------------+-------+-----------+----------+---------
 tablet_id           | text  |           |          |
 table_id            | text  |           |          |
 table_type          | text  |           |          |
 namespace_name      | text  |           |          |
 ysql_schema_name    | text  |           |          |
 table_name          | text  |           |          |
 partition_key_start | bytea |           |          |
 partition_key_end   | bytea |           |          |
```

### Get basic information

This example includes a YCQL table, a [hash-partitioned](../../../architecture/docdb-sharding/sharding/#hash-sharding) YSQL table, and a [range-partitioned](../../../architecture/docdb-sharding/sharding/#range-sharding) YSQL table.

```sql
yugabyte=# SELECT * FROM yb_local_tablets ORDER BY table_name, partition_key_start ASC NULLS FIRST;
```

```output
            tablet_id             |             table_id             | table_type | namespace_name  | ysql_schema_name |       table_name        |  partition_key_start   |   partition_key_end
----------------------------------+----------------------------------+------------+-----------------+------------------+-------------------------+------------------------+------------------------
 230de13ea3f045c2bc817046c96bfb9e | db82083fb39e47b0976b99f3612fa144 | YCQL       | ybdemo_keyspace |                  | cassandrakeyvalue       |                        | \x8000
 cb8ef7044b094709870d421fccd568a4 | db82083fb39e47b0976b99f3612fa144 | YCQL       | ybdemo_keyspace |                  | cassandrakeyvalue       | \x8000                 |
 76010b63fc714389ab97b432d9db78ac | 000033c1000030008000000000004000 | YSQL       | postgres        | public           | postgresqlkeyvalue      |                        | \x8000
 a5913f11706c4d8a80d74b7001dfe157 | 000033c1000030008000000000004000 | YSQL       | postgres        | public           | postgresqlkeyvalue      | \x8000                 |
 110ae7c832e7418bbfb56222a3e6a7ca | 000033c3000030008000000000004006 | YSQL       | yugabyte        | public           | range_partitioned_table |                        | \x48800000015361000021
 746f84ac4a894b9c914fd4a89d5f89fc | 000033c3000030008000000000004006 | YSQL       | yugabyte        | public           | range_partitioned_table | \x48800000015361000021 | \x48800000025362000021
 f584ca3aa57e43278fd4b5042ab116be | 000033c3000030008000000000004006 | YSQL       | yugabyte        | public           | range_partitioned_table | \x48800000025362000021 | \x48800000035363000021
 6d566b767f0347879934338e1642f58e | 000033c3000030008000000000004006 | YSQL       | yugabyte        | public           | range_partitioned_table | \x48800000035363000021 |
 bc90fa993cc340458d7d4500213e5aed | 4c9c54fb3fcc47dcb29e58899afc5e21 | System     | system          |                  | transactions            |                        | \x2000
 d106f1c5039a4127bf1bee83c5c3fec8 | 4c9c54fb3fcc47dcb29e58899afc5e21 | System     | system          |                  | transactions            | \x2000                 | \x4000
 045af4a5aa744e1fb06e41f4af134ee0 | 4c9c54fb3fcc47dcb29e58899afc5e21 | System     | system          |                  | transactions            | \x4000                 | \x6000
 0f63fe4806824a4ab17b0fd9cf144b8a | 4c9c54fb3fcc47dcb29e58899afc5e21 | System     | system          |                  | transactions            | \x6000                 | \x8000
 a7251899b197456fbec72f7cc64cc7ad | 4c9c54fb3fcc47dcb29e58899afc5e21 | System     | system          |                  | transactions            | \x8000                 | \xa000
 46035a0bc4144f8ea372c93dc5d3a8b6 | 4c9c54fb3fcc47dcb29e58899afc5e21 | System     | system          |                  | transactions            | \xa000                 | \xc000
 1a13e5b16aa841608390c56e63deab20 | 4c9c54fb3fcc47dcb29e58899afc5e21 | System     | system          |                  | transactions            | \xc000                 | \xe000
 5b452227726444a78d1c84aaaf44f5c0 | 4c9c54fb3fcc47dcb29e58899afc5e21 | System     | system          |                  | transactions            | \xe000                 |
```

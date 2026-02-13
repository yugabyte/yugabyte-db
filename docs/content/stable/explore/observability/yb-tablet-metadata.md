---
title: View cluster-wide tablet metadata and leadership
linkTitle: Cluster tablet metadata
description: View cluster-wide tablet distribution and leadership information for YSQL tables.
headerTitle: View YSQL cluster-wide tablet metadata and leadership information
tags:
  feature: early-access
menu:
  stable:
    identifier: yb-tablet-metadata
    parent: explore-observability
    weight: 321
type: docs
---

The `yb_tablet_metadata` view provides a YSQL-accessible interface for fetching tablet distribution and leadership information across a YugabyteDB cluster.

While the [yb_local_tablets](../yb-local-tablets/) view provides information about tablets on the local node, `yb_tablet_metadata` exposes tablet placement and replica roles cluster-wide, serving as the YSQL equivalent of the YCQL `system.partitions` table.

The `yb_tablet_metadata` view is useful for:

- Identifying the location of all tablets for a specific table.
- Determining the leader node for a specific tablet.
- Identifying the tablet for a given tuple, in case of [hash-sharded](../../../architecture/docdb-sharding/sharding/#hash-sharding) tables.

Note that the view returns tablet information for YSQL objects and the system transaction table only.

The following table describes the columns of the `yb_tablet_metadata` view.

| Column | Type | Description |
| :----- | :--- | :---------- |
| tablet_id | text | A unique identifier (UUID) representing the tablet. |
| oid | oid | The object identifier (OID) for the table/index that the tablet belongs to. |
| db_name | text | Name of the database this relation belongs to. |
| relname | text | Name of table/index whose data is stored on the tablet. |
| start_hash_code | int | Starting hash code (inclusive) for the tablet. (NULL for range-sharded tables.) |
| end_hash_code | int | Ending hash code (exclusive) for the tablet. (NULL for range-sharded tables.) |
| leader | text | IP address, port of the leader node for the tablet. |
| replicas | text[] | A list of replica IP addresses and port (includes leader) associated with the tablet. |
| active_ssts_size | bigint[] | Per-replica SST files size in bytes, in the same order as replicas. |
| wals_size | bigint[] | Per-replica WAL files size in bytes, in the same order as replicas. |

## Examples

{{% explore-setup-single-new %}}

Because this view is accessible via YSQL, run your examples using [ysqlsh](../../../api/ysqlsh/#starting-ysqlsh).

### Select nodes for a particular table

To find all tablets, their leaders, and replicas for a specific table:

```sql
SELECT * FROM yb_tablet_metadata WHERE db_name = 'yugabyte' AND relname = 'test_table';
```

```output
+----------------------------------+-------+----------+-------------+-----------------+---------------+------------------+--------------------------------------------------------+
| tablet_id                        | oid   | db_name  | relname     | start_hash_code | end_hash_code | leader           | replicas                                               |
|----------------------------------+-------+----------+-------------+-----------------+---------------+------------------+--------------------------------------------------------|
| 3987b6a16bf94fbd92262744197350d7 | 16384 | yugabyte | test_table  | 0               | 10922         | 127.0.0.2:5433   | ['127.0.0.1:5433', '127.0.0.2:5433', '127.0.0.3:5433'] |
| dd50b59c7dcb493680093ffa5b195634 | 16384 | yugabyte | test_table  | 10922           | 21845         | 127.0.0.1:5433   | ['127.0.0.1:5433', '127.0.0.2:5433', '127.0.0.3:5433'] |
| bed5b3c3eee747e99622a4e21acf437a | 16384 | yugabyte | test_table  | 21845           | 32768         | 127.0.0.3:5433   | ['127.0.0.1:5433', '127.0.0.2:5433', '127.0.0.3:5433'] |
| da4bad5faa9f448f890cce57c775cd94 | 16384 | yugabyte | test_table  | 32768           | 43690         | 127.0.0.2:5433   | ['127.0.0.1:5433', '127.0.0.2:5433', '127.0.0.3:5433'] |
| 52176c704c614846bbd80f481678519e | 16384 | yugabyte | test_table  | 43690           | 54613         | 127.0.0.1:5433   | ['127.0.0.1:5433', '127.0.0.2:5433', '127.0.0.3:5433'] |
| ea252119fe774ba9bdc585504fae9398 | 16384 | yugabyte | test_table  | 54613           | 65536         | 127.0.0.3:5433   | ['127.0.0.1:5433', '127.0.0.2:5433', '127.0.0.3:5433'] |
+----------------------------------+-------+----------+-------------+-----------------+---------------+------------------+--------------------------------------------------------+
```

### Join with yb_servers

You can join `yb_tablet_metadata` with [`yb_servers()`](../../../api/ysql/exprs/func_yb_servers/) to find infrastructure details (cloud, region, zone) for the tablet leader.

```sql
SELECT
    ytm.tablet_id,
    ytm.oid,
    ytm.db_name,
    ytm.relname,
    ytm.start_hash_code,
    ytm.end_hash_code,
    ys.host,
    ys.port,
    ys.cloud,
    ys.region,
    ys.zone,
    ys.uuid as server_uuid,
    ys.universe_uuid
FROM yb_tablet_metadata ytm
JOIN yb_servers() ys
    ON split_part(ytm.leader, ':', 1) = ys.host
    AND split_part(ytm.leader, ':', 2)::int = ys.port
ORDER BY ytm.start_hash_code;
```

```output
+----------------------------------+-------+----------+------------+-----------------+---------------+-----------+------+--------+-------------+-------+----------------------------------+--------------------------------------+
| tablet_id                        | oid   | db_name  | relname    | start_hash_code | end_hash_code | host      | port | cloud  | region      | zone  | server_uuid                      | universe_uuid                        |
|----------------------------------+-------+----------+------------+-----------------+---------------+-----------+------+--------+-------------+-------+----------------------------------+--------------------------------------+
| 3987b6a16bf94fbd92262744197350d7 | 16384 | yugabyte | test_table | 0               | 10922         | 127.0.0.2 | 5433 | cloud1 | datacenter1 | rack1 | 0e7b350cc6db42c3a5f0fde35352700f | 68a94218-f52d-428d-b95b-f98122d19035 |
| dd50b59c7dcb493680093ffa5b195634 | 16384 | yugabyte | test_table | 10922           | 21845         | 127.0.0.1 | 5433 | cloud1 | datacenter1 | rack1 | 228be93b9b164d8c99dc775e4acc805f | 68a94218-f52d-428d-b95b-f98122d19035 |
| bed5b3c3eee747e99622a4e21acf437a | 16384 | yugabyte | test_table | 21845           | 32768         | 127.0.0.3 | 5433 | cloud1 | datacenter1 | rack1 | ad618d063e2f4980bb3bc74a1a296565 | 68a94218-f52d-428d-b95b-f98122d19035 |
| da4bad5faa9f448f890cce57c775cd94 | 16384 | yugabyte | test_table | 32768           | 43690         | 127.0.0.2 | 5433 | cloud1 | datacenter1 | rack1 | 0e7b350cc6db42c3a5f0fde35352700f | 68a94218-f52d-428d-b95b-f98122d19035 |
| 52176c704c614846bbd80f481678519e | 16384 | yugabyte | test_table | 43690           | 54613         | 127.0.0.1 | 5433 | cloud1 | datacenter1 | rack1 | 228be93b9b164d8c99dc775e4acc805f | 68a94218-f52d-428d-b95b-f98122d19035 |
| ea252119fe774ba9bdc585504fae9398 | 16384 | yugabyte | test_table | 54613           | 65536         | 127.0.0.3 | 5433 | cloud1 | datacenter1 | rack1 | ad618d063e2f4980bb3bc74a1a296565 | 68a94218-f52d-428d-b95b-f98122d19035 |
+----------------------------------+-------+----------+------------+-----------------+---------------+-----------+------+--------+-------------+-------+----------------------------------+--------------------------------------+
```

### Find a tablet for a given key

Use the [yb_hash_code()](../../../api/ysql/exprs/func_yb_hash_code/) function to find the `tablet_id` and leader node for a given key in hash-partioned tables.

1. Check the table structure:

    ```sql
    \d test_table
    ```

    ```output
    +--------+------------------+-----------+
    | Column | Type             | Modifiers |
    |--------+------------------+-----------+
    | a      | text             |  not null |
    | b      | integer          |           |
    | c      | double precision |           |
    +--------+------------------+-----------+
    Indexes:
        "test_table_pkey" PRIMARY KEY, lsm (a HASH)
    ```

    Note that `a` is the only primary key.

1. Get the hash code for a specific key:

    ```sql
    SELECT *, yb_hash_code(a) FROM test_table WHERE a = 'k1';
    ```

    ```output
    +----------+------+------+--------------+
    | a        | b    | c    | yb_hash_code |
    |----------+------+------+--------------+
    | k1       | 2016 | 10.2 | 8000         |
    +----------+------+------+--------------+
    ```

1. Query `yb_tablet_metadata` to find which tablet this key belongs to:

    ```sql
    SELECT
        yb_hash_code('k1'::text) as hash_code,
        t.tablet_id,
        t.start_hash_code,
        t.end_hash_code,
        t.leader
    FROM yb_tablet_metadata t
    WHERE t.relname = 'test_table'
      AND yb_hash_code('k1'::text) >= t.start_hash_code
      AND yb_hash_code('k1'::text) < t.end_hash_code;
    ```

    ```output
    +-----------+----------------------------------+-----------------+---------------+----------------+
    | hash_code | tablet_id                        | start_hash_code | end_hash_code | leader         |
    |-----------+----------------------------------+-----------------+---------------+----------------|
    | 8000      | 6e60770b44ad49489ccb6949b8131c0e | 0               | 10922         | 127.0.0.3:5433 |
    +-----------+----------------------------------+-----------------+---------------+----------------+
    ```

1. To find the `tablet_id` for all rows of the table, use the following query:

    ```sql
    SELECT
        tt.a,
        tt.b,
        tt.c,
        yb_hash_code(tt.a) as hash_code,
        ytm.tablet_id,
        ytm.start_hash_code,
        ytm.end_hash_code,
        ytm.leader
    FROM test_table tt
    JOIN yb_tablet_metadata ytm
      ON yb_hash_code(tt.a) >= ytm.start_hash_code
        AND yb_hash_code(tt.a) < ytm.end_hash_code
    ORDER BY tt.a;
    ```

    ```output
    +----------------------------------+------+-------+-----------+----------------------------------+-----------------+---------------+----------------+
    | a                                | b    | c     | hash_code | tablet_id                        | start_hash_code | end_hash_code | leader         |
    |----------------------------------+------+-------+-----------+----------------------------------+-----------------+---------------+----------------|
    | github.com/yugabyte/yugabyte-db/ | 2020 | 108.2 | 58057     | 78c89b1113cf4f3ea1cd439851804ce1 | 54613           | 65536         | 127.0.0.3:5433 |
    | yb                               | 2020 | 108.2 | 5962      | 6e60770b44ad49489ccb6949b8131c0e | 0               | 10922         | 127.0.0.1:5433 |
    | ybdb                             | 2020 | 108.2 | 44116     | 6df5e7b1746b4c2b97bada5d49e5083a | 43690           | 54613         | 127.0.0.2:5433 |
    | yugabyte                         | 2016 | 10.2  | 8000      | 6e60770b44ad49489ccb6949b8131c0e | 0               | 10922         | 127.0.0.1:5433 |
    | yugabytedb                       | 2016 | 10.2  | 30096     | 97b6f53d462b4c2381a19f6285e4779e | 21845           | 32768         | 127.0.0.2:5433 |
    +----------------------------------+------+-------+-----------+----------------------------------+-----------------+---------------+----------------+
    ```

{{<tip title="Get hash codes in YCQL">}}
To obtain hash codes in YCQL, you can use the `partition_hash()` function, which, similar to `yb_hash_code()`, also dumps hash codes. You can use the `partition_hash()` function in YCQL to link rows with their tablets.
{{</tip>}}

### Join with Active Session History

Join [`yb_active_session_history`](../../../launch-and-manage/monitor-and-alert/active-session-history-monitor/#yb-active-session-history) with `yb_tablet_metadata` on `tablet_id` to get the metadata of a tablet that is part of `wait_event_aux`.

```sql
SELECT
     tablet_id,
     db_name,
     relname,
     start_hash_code,
     end_hash_code,
     wait_event,
     wait_event_component,
     wait_event_type,
     COUNT(*)
 FROM
     yb_active_session_history
 JOIN
     yb_tablet_metadata
 ON
     wait_event_aux = SUBSTRING(tablet_id, 1, 15)
 GROUP BY
     tablet_id,
     db_name,
     relname,
     start_hash_code,
     end_hash_code,
     wait_event,
     wait_event_component,
     wait_event_type
 ORDER BY
     relname,
     count DESC;
```

```output
+----------------------------------+----------+------------------+-----------------+---------------+------------------------------------+----------------------+-----------------+-------+
| tablet_id                        | db_name  | relname          | start_hash_code | end_hash_code | wait_event                         | wait_event_component | wait_event_type | count |
|----------------------------------+----------+------------------+-----------------+---------------+------------------------------------+----------------------+-----------------+-------+
| dfc98983540e4b98b4e3b39041f65ab3 | yugabyte | idx_test_table_b | 32768           | 65536         | Raft_ApplyingEdits                 | TServer              | Cpu             | 3     |
| dc151ee1a59745d69201290307862b7d | yugabyte | idx_test_table_b | 0               | 32768         | Raft_ApplyingEdits                 | TServer              | Cpu             | 2     |
| dc151ee1a59745d69201290307862b7d | yugabyte | idx_test_table_b | 0               | 32768         | ConflictResolution_ResolveConficts | TServer              | RPCWait         | 2     |
| dc151ee1a59745d69201290307862b7d | yugabyte | idx_test_table_b | 0               | 32768         | OnCpu_Active                       | TServer              | Cpu             | 2     |
| dfc98983540e4b98b4e3b39041f65ab3 | yugabyte | idx_test_table_b | 32768           | 65536         | OnCpu_Active                       | TServer              | Cpu             | 2     |
| dfc98983540e4b98b4e3b39041f65ab3 | yugabyte | idx_test_table_b | 32768           | 65536         | ConflictResolution_ResolveConficts | TServer              | RPCWait         | 2     |
| 1f7aee0ceea445959129b23285756dad | yugabyte | idx_test_table_c | 0               | 32768         | Raft_ApplyingEdits                 | TServer              | Cpu             | 20    |
| b6eea846ecd9433f89ea7ccaadff8cec | yugabyte | idx_test_table_c | 32768           | 65536         | Raft_ApplyingEdits                 | TServer              | Cpu             | 19    |
| 1f7aee0ceea445959129b23285756dad | yugabyte | idx_test_table_c | 0               | 32768         | OnCpu_Active                       | TServer              | Cpu             | 19    |
| b6eea846ecd9433f89ea7ccaadff8cec | yugabyte | idx_test_table_c | 32768           | 65536         | OnCpu_Active                       | TServer              | Cpu             | 19    |
| 1f7aee0ceea445959129b23285756dad | yugabyte | idx_test_table_c | 0               | 32768         | ConflictResolution_ResolveConficts | TServer              | RPCWait         | 10    |
| b6eea846ecd9433f89ea7ccaadff8cec | yugabyte | idx_test_table_c | 32768           | 65536         | ConflictResolution_ResolveConficts | TServer              | RPCWait         | 10    |
| 1f7aee0ceea445959129b23285756dad | yugabyte | idx_test_table_c | 0               | 32768         | OnCpu_Passive                      | TServer              | Cpu             | 1     |
| b6eea846ecd9433f89ea7ccaadff8cec | yugabyte | idx_test_table_c | 32768           | 65536         | OnCpu_Passive                      | TServer              | Cpu             | 1     |
| 99ed7472ccde4af787cb0bcfd4fd90bd | yugabyte | test_table       | 0               | 32768         | OnCpu_Passive                      | TServer              | Cpu             | 83    |
| c853133c0db445928ae1262c01238d2d | yugabyte | test_table       | 32768           | 65536         | OnCpu_Passive                      | TServer              | Cpu             | 83    |
| c853133c0db445928ae1262c01238d2d | yugabyte | test_table       | 32768           | 65536         | OnCpu_Active                       | TServer              | Cpu             | 51    |
| 99ed7472ccde4af787cb0bcfd4fd90bd | yugabyte | test_table       | 0               | 32768         | OnCpu_Active                       | TServer              | Cpu             | 49    |
| 99ed7472ccde4af787cb0bcfd4fd90bd | yugabyte | test_table       | 0               | 32768         | Raft_ApplyingEdits                 | TServer              | Cpu             | 34    |
| c853133c0db445928ae1262c01238d2d | yugabyte | test_table       | 32768           | 65536         | Raft_ApplyingEdits                 | TServer              | Cpu             | 33    |
| 99ed7472ccde4af787cb0bcfd4fd90bd | yugabyte | test_table       | 0               | 32768         | ConflictResolution_ResolveConficts | TServer              | RPCWait         | 15    |
| c853133c0db445928ae1262c01238d2d | yugabyte | test_table       | 32768           | 65536         | ConflictResolution_ResolveConficts | TServer              | RPCWait         | 14    |
| c853133c0db445928ae1262c01238d2d | yugabyte | test_table       | 32768           | 65536         | WAL_Append                         | TServer              | DiskIO          | 2     |
| 99ed7472ccde4af787cb0bcfd4fd90bd | yugabyte | test_table       | 0               | 32768         | Rpc_Done                           | TServer              | WaitOnCondition | 1     |
| c853133c0db445928ae1262c01238d2d | yugabyte | test_table       | 32768           | 65536         | Raft_WaitingForReplication         | TServer              | RPCWait         | 1     |
| c853133c0db445928ae1262c01238d2d | yugabyte | test_table       | 32768           | 65536         | RocksDB_NewIterator                | TServer              | DiskIO          | 1     |
| 99ed7472ccde4af787cb0bcfd4fd90bd | yugabyte | test_table       | 0               | 32768         | RocksDB_NewIterator                | TServer              | DiskIO          | 1     |
+----------------------------------+----------+------------------+-----------------+---------------+------------------------------------+----------------------+-----------------+-------+
```

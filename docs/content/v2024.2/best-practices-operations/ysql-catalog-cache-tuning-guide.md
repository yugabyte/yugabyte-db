---
title: Customizing the preloading of YSQL catalog caches
headerTitle: Customize preloading of YSQL catalog caches
linkTitle: YSQL catalog cache tuning
description: Trading off memory vs performance in YSQL catalog caches
headcontent: Trading off memory vs performance in YSQL catalog caches
menu:
  v2024.2:
    identifier: ysql-catalog-cache-tuning-guide
    parent: best-practices-operations
    weight: 50
type: docs
---

Many common PostgreSQL operations, such as parsing a query, planning, and so on, require looking up entries in PostgreSQL system catalog tables, including pg_class, pg_operator, pg_statistic, and pg_attribute, for PostgreSQL metadata for the columns, operators, and more.

For example, the following shows the logging output for a SELECT statement with catalog caching output enabled.

<details>
  <summary>Catalog caching log output</summary>

```sql
SET yb_debug_log_catcache_events=1;
SET client_min_messages=LOG;
SELECT distinct key, count(*) FROM part_par GROUP BY KEY;
```

```output.yaml
LOG:  catalog cache miss on cache with id 35:
target rel: pg_namespace (oid : 2615), index oid 2684
search keys: yugabyte
LINE 1: select distinct key, count(*) from part_par group by key;
                                           ^
LOG:  catalog cache miss on cache with id 35:
target rel: pg_namespace (oid : 2615), index oid 2684
search keys: public
LINE 1: select distinct key, count(*) from part_par group by key;
                                           ^
LOG:  catalog cache miss on cache with id 54:
target rel: pg_class (oid : 1259), index oid 2663
search keys: part_par, 11
LINE 1: select distinct key, count(*) from part_par group by key;
                                           ^
LOG:  catalog cache miss on cache with id 54:
target rel: pg_class (oid : 1259), index oid 2663
search keys: part_par, 2200
LINE 1: select distinct key, count(*) from part_par group by key;
                                           ^
LOG:  catalog cache miss on cache with id 2:
target rel: pg_am (oid : 2601), index oid 2652
search keys: 2
LINE 1: select distinct key, count(*) from part_par group by key;
                                           ^
LOG:  catalog cache miss on cache with id 45:
target rel: pg_proc (oid : 1255), index oid 2690
search keys: 2803
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 0:
target rel: pg_aggregate (oid : 2600), index oid 2650
search keys: pg_catalog.count
LOG:  catalog cache miss on cache with id 80:
target rel: pg_type (oid : 1247), index oid 2703
search keys: 23
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 23, 2277
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 23, 1560
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 23, 16
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 23, 1042
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 23, 17
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 23, 18
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 23, 1082
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 23, 3500
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 23, 700
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 23, 701
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 23, 869
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 23, 21
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 14:
target rel: pg_opclass (oid : 2616), index oid 2687
search keys: 9942
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 23, 1033
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 23, 29
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 14:
target rel: pg_opclass (oid : 2616), index oid 2687
search keys: 10020
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 4:
target rel: pg_amop (oid : 2602), index oid 2653
search keys: 9912, 23, 23, 3
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 4:
target rel: pg_amop (oid : 2602), index oid 2653
search keys: 9912, 23, 23, 1
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 4:
target rel: pg_amop (oid : 2602), index oid 2653
search keys: 9912, 23, 23, 5
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 4:
target rel: pg_amop (oid : 2602), index oid 2653
search keys: 1977, 23, 23, 1
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 5:
target rel: pg_amproc (oid : 2603), index oid 2655
search keys: 1977, 23, 23, 1
LINE 1: select distinct key, count(*) from part_par group by key;
                                                             ^
LOG:  catalog cache miss on cache with id 80:
target rel: pg_type (oid : 1247), index oid 2703
search keys: 20
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 20, 2277
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 20, 1560
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 20, 16
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 20, 1042
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 20, 17
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 20, 18
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 20, 1082
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 20, 3500
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 20, 700
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 20, 701
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 20, 869
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 20, 21
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 20, 23
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 14:
target rel: pg_opclass (oid : 2616), index oid 2687
search keys: 9943
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 20, 1033
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 12:
target rel: pg_cast (oid : 2605), index oid 2661
search keys: 20, 29
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 14:
target rel: pg_opclass (oid : 2616), index oid 2687
search keys: 10021
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 4:
target rel: pg_amop (oid : 2602), index oid 2653
search keys: 9912, 20, 20, 3
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 4:
target rel: pg_amop (oid : 2602), index oid 2653
search keys: 9912, 20, 20, 1
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 4:
target rel: pg_amop (oid : 2602), index oid 2653
search keys: 9912, 20, 20, 5
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 4:
target rel: pg_amop (oid : 2602), index oid 2653
search keys: 1977, 20, 20, 1
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 5:
target rel: pg_amproc (oid : 2603), index oid 2655
search keys: 1977, 20, 20, 1
LINE 1: select distinct key, count(*) from part_par group by key;
                             ^
LOG:  catalog cache miss on cache with id 55:
target rel: pg_class (oid : 1259), index oid 2662
search keys: 16907
LOG:  catalog cache miss on cache with id 32:
target rel: pg_index (oid : 2610), index oid 2679
search keys: 3379
LOG:  catalog cache miss on cache with id 2:
target rel: pg_am (oid : 2601), index oid 2652
search keys: 9900
LOG:  catalog cache miss on cache with id 7:
target rel: pg_attribute (oid : 1249), index oid 2659
search keys: 3379, 1
LOG:  catalog cache miss on cache with id 43:
target rel: pg_partitioned_table (oid : 3350), index oid 3351
search keys: 16907
LOG:  catalog cache miss on cache with id 5:
target rel: pg_amproc (oid : 2603), index oid 2655
search keys: 9912, 23, 23, 1
LOG:  catalog cache miss on cache with id 32:
target rel: pg_index (oid : 2610), index oid 2679
search keys: 2187
LOG:  catalog cache miss on cache with id 7:
target rel: pg_attribute (oid : 1249), index oid 2659
search keys: 2187, 1
LOG:  catalog cache miss on cache with id 55:
target rel: pg_class (oid : 1259), index oid 2662
search keys: 16912
LOG:  catalog cache miss on cache with id 55:
target rel: pg_class (oid : 1259), index oid 2662
search keys: 16917
LOG:  catalog cache miss on cache with id 55:
target rel: pg_class (oid : 1259), index oid 2662
search keys: 16922
LOG:  catalog cache miss on cache with id 4:
target rel: pg_amop (oid : 2602), index oid 2653
search keys: 1976, 23, 23, 3
LOG:  catalog cache miss on cache with id 4:
target rel: pg_amop (oid : 2602), index oid 2653
search keys: 1976, 20, 20, 3
LOG:  catalog cache miss on cache with id 32:
target rel: pg_index (oid : 2610), index oid 2679
search keys: 2678
LOG:  catalog cache miss on cache with id 7:
target rel: pg_attribute (oid : 1249), index oid 2659
search keys: 2678, 1
LOG:  catalog cache miss on cache with id 32:
target rel: pg_index (oid : 2610), index oid 2679
search keys: 16915
LOG:  catalog cache miss on cache with id 7:
target rel: pg_attribute (oid : 1249), index oid 2659
search keys: 16915, 1
LOG:  catalog cache miss on cache with id 7:
target rel: pg_attribute (oid : 1249), index oid 2659
search keys: 16915, 2
LOG:  catalog cache miss on cache with id 32:
target rel: pg_index (oid : 2610), index oid 2679
search keys: 16916
LOG:  catalog cache miss on cache with id 7:
target rel: pg_attribute (oid : 1249), index oid 2659
search keys: 16916, 1
LOG:  catalog cache miss on cache with id 32:
target rel: pg_index (oid : 2610), index oid 2679
search keys: 16928
LOG:  catalog cache miss on cache with id 7:
target rel: pg_attribute (oid : 1249), index oid 2659
search keys: 16928, 1
LOG:  catalog cache miss on cache with id 7:
target rel: pg_attribute (oid : 1249), index oid 2659
search keys: 16928, 2
LOG:  catalog cache miss on cache with id 32:
target rel: pg_index (oid : 2610), index oid 2679
search keys: 16920
LOG:  catalog cache miss on cache with id 7:
target rel: pg_attribute (oid : 1249), index oid 2659
search keys: 16920, 1
LOG:  catalog cache miss on cache with id 7:
target rel: pg_attribute (oid : 1249), index oid 2659
search keys: 16920, 2
LOG:  catalog cache miss on cache with id 32:
target rel: pg_index (oid : 2610), index oid 2679
search keys: 16921
LOG:  catalog cache miss on cache with id 7:
target rel: pg_attribute (oid : 1249), index oid 2659
search keys: 16921, 1
LOG:  catalog cache miss on cache with id 32:
target rel: pg_index (oid : 2610), index oid 2679
search keys: 16929
LOG:  catalog cache miss on cache with id 7:
target rel: pg_attribute (oid : 1249), index oid 2659
search keys: 16929, 1
LOG:  catalog cache miss on cache with id 7:
target rel: pg_attribute (oid : 1249), index oid 2659
search keys: 16929, 2
LOG:  catalog cache miss on cache with id 32:
target rel: pg_index (oid : 2610), index oid 2679
search keys: 16925
LOG:  catalog cache miss on cache with id 7:
target rel: pg_attribute (oid : 1249), index oid 2659
search keys: 16925, 1
LOG:  catalog cache miss on cache with id 7:
target rel: pg_attribute (oid : 1249), index oid 2659
search keys: 16925, 2
LOG:  catalog cache miss on cache with id 32:
target rel: pg_index (oid : 2610), index oid 2679
search keys: 16926
LOG:  catalog cache miss on cache with id 7:
target rel: pg_attribute (oid : 1249), index oid 2659
search keys: 16926, 1
LOG:  catalog cache miss on cache with id 32:
target rel: pg_index (oid : 2610), index oid 2679
search keys: 16930
LOG:  catalog cache miss on cache with id 7:
target rel: pg_attribute (oid : 1249), index oid 2659
search keys: 16930, 1
LOG:  catalog cache miss on cache with id 7:
target rel: pg_attribute (oid : 1249), index oid 2659
search keys: 16930, 2
LOG:  catalog cache miss on cache with id 67:
target rel: pg_tablespace (oid : 1213), index oid 2697
search keys: 1663
LOG:  catalog cache miss on cache with id 45:
target rel: pg_proc (oid : 1255), index oid 2690
search keys: 1219
LOG:  catalog cache miss on cache with id 63:
target rel: pg_statistic (oid : 2619), index oid 2696
search keys: 16907, 2, t
LOG:  catalog cache miss on cache with id 38:
target rel: pg_operator (oid : 2617), index oid 2688
search keys: 96
LOG:  catalog cache miss on cache with id 38:
target rel: pg_operator (oid : 2617), index oid 2688
search keys: 410
 key | count
-----+-------
   1 |     1
  11 |     1
```

</details>

&nbsp;

Each PostgreSQL backend (process) caches such metadata for performance reasons. In YugabyteDB, misses on these caches need to be loaded from the YB-Master leader. As a result, initial queries on that backend can be slow until these caches are warm, especially if the YB-Master leader is in a different region.

## Key settings

There is a tradeoff between:

- Latency of loading these catalog entries from a remote YB-Master; and
- Memory cost of prepopulating entries ahead of time.

You can customize this tradeoff to control preloading of entries into PostgreSQL caches.

### Default behavior

By default, YSQL backends do not preload any catalog entries, except right after a schema change (DDL).

On most schema changes, running backends discard their catalog cache, and are then refreshed from either the YB-Master leader or an intermediate response cache on the local YB-TServer. This refresh causes a latency hit on running queries while they wait for this process to complete. There is also a memory increase because the cache is now preloaded with all rows of these catalog tables (as opposed to just the actively used entries that it had before).

## Problem scenarios and recommendations

The following are some scenarios where you may want to tweak the catalog cache preloading behavior.

### Initial queries on a new connection are slow

Initial queries on new connections may be slightly slower until the PostgreSQL catalog cache is warmed up. This effect may be particularly significant in two cases:

1. Multi-region clusters (where the master leader is far away from the PostgreSQL backend).
1. High connection churn. That is, when the client does not use a steady pool of connections.

To confirm that catalog caching is the cause of this initial latency, see [Confirm that catalog cache misses are a root cause of latency / load](#confirm-catalog-cache-misses-root-cause-of-latency).

**Possible solutions**

- [Use connection pooling](#connection-pooling) to reuse existing connections.
- [Preload additional system tables](#preload-additional-system-tables).

### High CPU load on YB-Master leader

If the client does not have a steady pool of connections, the resulting connection churn may cause high load on the YB-Master leader as these caches are warmed up repeatedly on each new PostgreSQL connection.

To confirm that catalog caching is the cause of this, see [Confirm that catalog cache misses are a root cause of latency / load](#confirm-catalog-cache-misses-root-cause-of-latency).

**Possible solutions**

- [Use connection pooling](#connection-pooling) to reuse existing connections.
- [Preload additional system tables](#preload-additional-system-tables).

### Memory spikes on PostgreSQL backends or out of memory (OOM) events

On the flip side, automatic preloading of caches after a DDL change may cause memory spikes on PostgreSQL backends or out of memory (OOM) events.

To confirm that catalog caching is the cause of this, correlate the time when DDLs were run (Write RPCs on YB-Master) to the time of the OOM event or a spike in PostgreSQL RSS metrics.

**Possible solution**

- [Minimal catalog cache preloading](#minimal-catalog-cache-preloading).

## Solution details

### Use connection pooling {#connection-pooling}

When there is significant connection churn, the warm up of catalog caches on each new connection can cause high initial client latency and CPU load on the YB-Master leader process. Connection pooling allows better reuse of connections across different queries, so more queries should land on a backend with a warm cache.

To set up connection pooling, explore the following approaches:

- [Server-side connection pooling](../../explore/going-beyond-sql/connection-mgr-ysql/) using YSQL Connection Manager ({{<tags/feature/ea idea="1368">}} in v2024.2).
- [Client-side connection pooling](/stable/develop/drivers-orms/smart-drivers/#connection-pooling) using smart drivers.
- [Intermediate connection pooling](https://www.yugabyte.com/blog/database-connection-management/) through tools like PgBouncer and Odyssey.

### Preload additional system tables {#preload-additional-system-tables}

When enabled, all catalog cache entries corresponding to specific PostgreSQL catalog tables are preloaded (both on regular PostgreSQL backend startup and after a schema change). This decreases warm up time for these caches, thereby decreasing the latency impact of initial queries on new connections. The downside is that it causes more memory consumption on all backends.

To preload system tables, set the [YB-TServer flag](../../reference/configuration/yb-tserver/#catalog-flags)  `--ysql_catalog_preload_additional_tables=true` to preload caches for the following tables:

- pg_am
- pg_amproc
- pg_cast
- pg_inherits
- pg_policy
- pg_proc
- pg_tablespace
- pg_trigger

To customize preloading in a more granular way, refer to [Identify the specific tables to preload](#identify-the-specific-tables-to-be-preloaded).

Set the [YB-TServer flag](../../reference/configuration/yb-tserver/#catalog-flags) `--ysql_catalog_preload_additional_table_list=\<list of pg tables\>` to populate caches for specified tables in addition to the default list.

For example:

```yaml
--ysql_catalog_preload_additional_table_list=pg_operator,pg_amop,pg_cast,pg_aggregate
```

### Minimal catalog cache preloading {#minimal-catalog-cache-preloading}

When enabled, only a small subset of the catalog cache entries is preloaded. This reduces the memory spike that results, but can increase the warm up time for queries after a DDL change, as well as the initial query latency when [additional tables are preloaded](#preload-additional-system-tables).

To enable minimal catalog cache preloading, set the [YB-TServer flag](../../reference/configuration/yb-tserver/#catalog-flags) `--ysql_minimal_catalog_caches_preload=true`.

## Confirm catalog cache misses are a root cause of latency / load {#confirm-catalog-cache-misses-root-cause-of-latency}

To confirm that catalog cache misses are a cause of latency or load, use the following techniques:

1. Run [EXPLAIN (ANALYZE, DIST) \<query\>](../../launch-and-manage/monitor-and-alert/query-tuning/explain-analyze/#:~:text=Index%20Writes.-,Catalog%20Read%20Requests,-%3A%20Number%20of%20requests) on the first query on a new connection that shows a high number of Catalog Reads. A subsequent run of the same EXPLAIN (ANALYZE, DIST) typically shows a drop in the number of Catalog Reads.

2. Check metrics for a [high number](https://docs.yugabyte.com/images/yp/metrics114.png) of [Catalog Cache Misses](../../yugabyte-platform/alerts-monitoring/anywhere-metrics/#ysql-ops-and-latency:~:text=on%20other%20metrics.-,Catalog%20Cache%20Misses,-During%20YSQL%20query).

    There should be a [corresponding rate of increase of YB-Master Read RPCs](../../launch-and-manage/monitor-and-alert/metrics/ybmaster/#:~:text=handler_latency_yb_tserver_TabletServerService_Read).

3. [YBA Performance Advisor](../../yugabyte-platform/alerts-monitoring/performance-advisor/) shows the anomaly **Excessive Catalog Reads**.

4. [Manually collect logs](#manually-collecting-logs-for-catalog-reads).

## Identify specific tables to be preloaded {#identify-the-specific-tables-to-be-preloaded}

From the [Catalog Cache Misses](../../yugabyte-platform/alerts-monitoring/anywhere-metrics/#ysql-ops-and-latency:~:text=on%20other%20metrics.-,Catalog%20Cache%20Misses,-During%20YSQL%20query) metrics dashboard, identify the PostgreSQL catalog table names that are causing the highest misses.

You can do this manually or by using the Outlier Tables view on the dashboard. After you identify the top N catalog tables, add them one by one to the `--ysql_catalog_preload_additional_table_list` flag until the first connection latency is acceptable. It might be sufficient to just set `--ysql_catalog_preload_additional_tables=true` in most cases.

If there are still a significant number of misses to these tables after preloading them, [manually collect logs](#manually-collecting-logs-for-catalog-reads) and share them with Yugabyte Support.

## Manually collect logs for catalog reads {#manually-collecting-logs-for-catalog-reads}

If the catalog reads can be traced to a specific query, set the following configuration parameters and run [EXPLAIN (ANALYZE, DIST) \<query\>](../../launch-and-manage/monitor-and-alert/query-tuning/explain-analyze/#:~:text=Distributed%20Storage%20Counters):

```sql
SET yb_debug_log_catcache_events = 1;
SET yb_debug_report_error_stacktrace = 1;
SET client_min_messages = LOG;
```

Collect the output from this session.

If you are unable to identify a specific query, set the flag [`--ysql_pg_conf_csv`](../../reference/configuration/all-flags-yb-tserver/#ysql-pg-conf-csv) to include  `yb_debug_log_catcache_events=1` for a short duration and collect the PostgreSQL log file (doing so may affect performance significantly).

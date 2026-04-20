---
title: Query plan manager
linkTitle: Query plan manager
description: Capture all unique plans for a query so that you can track plan changes
headerTitle: Query plan manager
headcontent: Monitor plan changes
menu:
  stable:
    identifier: query-plan-manager
    parent: query-tuning
    weight: 600
type: docs
---

Enterprises need stable and consistent performance during various system changes on YugabyteDB databases. One of the major causes of response time variability is query plan instability. Various factors can unexpectedly change the execution plan of queries. For example:

- Change in optimizer statistics (manually or automatically)
- Changes to the query planner configuration parameters
- Changes to the schema, such as adding a new index
- Changes to the bind variables used in the query
- Minor version or major version upgrades to the database version. 

The query plan manager (QPM) serves two main objectives:

- Plan Stability. QPM prevents plan regression and improves plan stability when changes occur in the system.
- Plan Adaptability. QPM automatically detects new minimum-cost plans and controls when new plans may be used and adapts to the changes.

Because plans can change over time (you turn on the cost-based optimizer, for example), there is a chance a new plan for a query degrades performance. The yb_pg_stat_plans view captures all unique plans for a query so that you can look back and see when a plan changed. The entry for the old plan will have a set of hints that can generate that plan. You can then insert those hints into the hint plan table and get the old better-performing plan back.

## Using QPM

QPM is enabled by default, and you view QPM results using two views:

- [yb_pg_stat_plans](#yb-pg-stat-plans)
- [yb_pg_stat_plans_get_insights](#yb-pg-stat-plans-get-insights)

If you notice a query is performing poorly, you can run [EXPLAIN](../../../../api/ysql/the-sql-language/statements/perf_explain/) to get its current query ID and/or plan ID, and then use this ID to check the yb_pg_stat_plans view to see if this is a new plan or if its execution time has recently increased, helping you detect plan regressions. For example:

```sql
EXPLAIN (queryid on, planid on) SELECT count(*) FROM t0, t1 WHERE a0=a1;
```

Currently, only SELECT, INSERT, UPDATE, MERGE, DELETE, and EXECUTE statements for which hints can be generated are tracked. EXPLAIN ANALYZE statements are not tracked.

If hints cannot be generated for a plan, then the plan is not stored. (For example, `SELECT 1`, or `SELECT * FROM func()`.)

Nested queries and queries referencing catalog tables may or may not be tracked, depending on the settings of the `yb_pg_stat_plans_track` and `yb_pg_stat_plans_track_catalog_queries` parameters.

If the string `__YB_STAT_PLANS_SKIP` is encountered in an SQL comment of a statement, QPM will not process the query. This provides a way to prevent QPM entries for selected queries.

A query may have multiple plans, such as when optimizer statistics change, or a new execution method becomes available.

For example, running the following 2 queries results in two entries in the QPM table, one for each plan:

```sql
/*+ MergeJoin(t0 t3) */ SELECT 1 FROM t0, t3 WHERE pk0_0 = pk3_0;

/*+ HashJoin(t0 t3) */ SELECT 1 FROM t0, t3 WHERE pk0_0 = pk3_0;
```

Multiple QueryIDs can also have the same plan:

```sql
EXPLAIN (queryid on, planid on) SELECT count(*) FROM t0, t1 WHERE a0=a1;
```

```caddyfile{.nocopy}
                                       QUERY PLAN                                       
----------------------------------------------------------------------------------------
 Aggregate  (cost=22.16..22.17 rows=1 width=8)
   ->  Nested Loop  (cost=0.01..21.85 rows=123 width=0)
         ->  Seq Scan on t0  (cost=0.00..10.00 rows=100 width=4)
         ->  Memoize  (cost=0.01..0.16 rows=1 width=4)
               Cache Key: t0.a0
               Cache Mode: logical
               ->  Index Only Scan using a1_idx on t1  (cost=0.00..0.15 rows=1 width=4)
                     Index Cond: (a1 = t0.a0)
 Query Identifier: 6117265437906440259
 Plan Identifier: 1344843950213355732
(10 rows)
```

```sql
yugabyte=# explain (queryid on, planid on) select count(*) from t1, t0 where a0=a1;
```

```caddyfile{.nocopy}
                                       QUERY PLAN                                       
----------------------------------------------------------------------------------------
 Aggregate  (cost=22.16..22.17 rows=1 width=8)
   ->  Nested Loop  (cost=0.01..21.85 rows=123 width=0)
         ->  Seq Scan on t0  (cost=0.00..10.00 rows=100 width=4)
         ->  Memoize  (cost=0.01..0.16 rows=1 width=4)
               Cache Key: t0.a0
               Cache Mode: logical
               ->  Index Only Scan using a1_idx on t1  (cost=0.00..0.15 rows=1 width=4)
                     Index Cond: (a1 = t0.a0)
 Query Identifier: -3910230868235372015
 Plan Identifier: 1344843950213355732
(10 rows)
```

```sql
yugabyte=# explain (queryid on, planid on) select count(*) from t0, t1 where a1=a0;
```

```caddyfile{.nocopy}
                                       QUERY PLAN                                       
----------------------------------------------------------------------------------------
 Aggregate  (cost=22.16..22.17 rows=1 width=8)
   ->  Nested Loop  (cost=0.01..21.85 rows=123 width=0)
         ->  Seq Scan on t0  (cost=0.00..10.00 rows=100 width=4)
         ->  Memoize  (cost=0.01..0.16 rows=1 width=4)
               Cache Key: t0.a0
               Cache Mode: logical
               ->  Index Only Scan using a1_idx on t1  (cost=0.00..0.15 rows=1 width=4)
                     Index Cond: (a1 = t0.a0)
 Query Identifier: -6001653384983356447
 Plan Identifier: 1344843950213355732
(10 rows)
```

```sql
yugabyte=# explain (queryid on, planid on) select count(*) from t1, t0 where a1=a0;
```

```caddyfile{.nocopy}
                                       QUERY PLAN                                       
----------------------------------------------------------------------------------------
 Aggregate  (cost=22.16..22.17 rows=1 width=8)
   ->  Nested Loop  (cost=0.01..21.85 rows=123 width=0)
         ->  Seq Scan on t0  (cost=0.00..10.00 rows=100 width=4)
         ->  Memoize  (cost=0.01..0.16 rows=1 width=4)
               Cache Key: t0.a0
               Cache Mode: logical
               ->  Index Only Scan using a1_idx on t1  (cost=0.00..0.15 rows=1 width=4)
                     Index Cond: (a1 = t0.a0)
 Query Identifier: 8157648196979716971
 Plan Identifier: 1344843950213355732
```

## Views

### yb_pg_stat_plans

The yb_pg_stat_plans view serves as the primary method for accessing QPM data. This view essentially wraps the `yb_pg_stat_plans_get_all_entries()` function. To illustrate, retrieving all QPM data can be achieved with the following query:

```sql
SELECT * FROM yb_pg_stat_plans;
```

```caddyfile{.nocopy}
                                    View "pg_catalog.yb_pg_stat_plans"
        Column        |           Type           | Collation | Nullable | Default | Storage  | Description 
----------------------+--------------------------+-----------+----------+---------+----------+-------------
 dbid                 | oid                      |           |          |         | plain    | 
 userid               | oid                      |           |          |         | plain    | 
 queryid              | bigint                   |           |          |         | plain    | 
 planid               | bigint                   |           |          |         | plain    | 
 first_used           | timestamp with time zone |           |          |         | plain    | 
 last_used            | timestamp with time zone |           |          |         | plain    | 
 hints                | text                     |           |          |         | extended | 
 calls                | bigint                   |           |          |         | plain    | 
 avg_exec_time        | double precision         |           |          |         | plain    | 
 max_exec_time        | double precision         |           |          |         | plain    | 
 max_exec_time_params | text                     |           |          |         | extended | 
 avg_est_cost         | double precision         |           |          |         | plain    | 
 plan                 | text                     |           |          |         | extended | 
View definition:
 SELECT stat_plans.dbid,
    stat_plans.userid,
    stat_plans.queryid,
    stat_plans.planid,
    stat_plans.first_used,
    stat_plans.last_used,
    stat_plans.hints,
    stat_plans.calls,
    stat_plans.avg_exec_time,
    stat_plans.max_exec_time,
    stat_plans.max_exec_time_params,
    stat_plans.avg_est_cost,
    stat_plans.plan
   FROM yb_pg_stat_plans_get_all_entries() stat_plans(dbid oid, userid oid, 
      queryid bigint, planid bigint, first_used timestamp with time zone, 
      last_used timestamp with time zone, hints text, calls bigint, 
      avg_exec_time double precision, max_exec_time double precision, 
      max_exec_time_params text, avg_est_cost double precision, plan text);
```

The columns of the yb_pg_stat_plans view are described in the following table.

| Column | Description |
| :----- | :---------- |
| userid | OID of user who executed the statement. |
| dbid | OID of database in which the statement was executed. |
| queryid | Hash code to identify identical normalized queries. |
| planid | Hash of a query's execution plan representation. |
| first_used | First recorded instance of the [dbid query id, plan id] use. |
| last_used | Last recorded instance of the [dbid query id, plan id] use. |
| calls | Number of times [dbid query id, plan id] pair is used. |
| avg_exec_time | Average execution time. |
| max_exec_time | Highest recorded execution time for the planMaximum recorded execution time for this plan. |
| max_exec_time_params | This particular set of query parameters led to the longest execution time. |
| avg_est_cost | Planner's average estimated cost for the plan. |
| hints | These hints, if applied during query planning, would lead to the same plan being used. |
| plan | Text representation of the plan. |

`yb_pg_stat_plans` does not track query text. You can retrive query text by joining with pg_stat_statements on queryid. For example:

```sql
SELECT CASE WHEN ss.query IS NOT NULL THEN ss.query ELSE '<NULL>' END as query_string, hints, … 
FROM yb_query_plan_management qpm LEFT JOIN pg_stat_statements ss ON qpm.queryId=ss.queryid;
```

yb_pg_stat_plans is pre-configured, and enabled by default. The following YSQL configuration parameters control yb_pg_stat_plans:

| Option | Description | Default |
| :----- | :---------- | :------ |
| `yb_pg_stat_plans_track` | Turns tracking on or off. Valid values are:<ul><li>none: no plans are tracked, disables QPM.</li><li>top: track only top-level (non-nested) statements.</li><li>all: track all statements.</li></ul> | all |
| `yb_pg_stat_plans_max_cache_size` | Specifies the Maximum number of entries to store (1-50000). | 5000 |
| `yb_pg_stat_plans_cache_replacement_algorithm` | Controls the eviction policy used by QPM once the number of entries reaches yb_pg_stat_plans_max_cache_size. Valid values are `simple_clock_lru` - uses an efficient simulation of clock-based Least Recently Used (LRU) algorithm to find the entry to evict; or `true_lru` - evicts the entry with the oldest last_used value (less efficient but finds the actual oldest entry). Requires restart. | simple_clock_lru |
| `yb_pg_stat_plans_track_catalog_queries` | Controls tracking of statements referencing catalog tables. | true |
| `yb_pg_stat_plans_verbose_plans` | Set to `true` to generate and store verbose plans. | false |
| `yb_pg_stat_plans_plan_format` | Controls text format for plans.<br/>Valid values are `text`, `json`, `yaml`, and `xml`. | json |

The yb_pg_stat_plans data is stored per node; all backends on a specific node read and write to the same table. The data is persisted across restarts.

The table is fixed size (default 5000 unique pairs), set using `yb_pg_stat_plans_max_cache_size`.

When the limit on the number of entries is reached, the oldest entry is dropped using a replacement strategy, set using `yb_pg_stat_plans_cache_replacement_algorithm`.

If the hint or plan text does not fit into the fixed size memory slot (4096 bytes for plan text, 2048 bytes for hint text), the text is compressed. If the hint or plan text does not fit after compression, then the text payload is truncated. Truncated hints can not be used to pin the plan.

### yb_pg_stat_plans_get_insights

This view identifies the plan with the lowest execution time for each query ID (`plan_min_exec_time` column displays `Yes`; otherwise `No`).

If a plan has the lowest estimated cost but not the lowest execution time, or has the lowest execution time but not the lowest estimated cost, the `plan_require_evaluation` column displays `Yes`; otherwise `No`.

```output
                                 View "pg_catalog.yb_pg_stat_plans_insights"
         Column          |           Type           | Collation | Nullable | Default | Storage  | Description 
-------------------------+--------------------------+-----------+----------+---------+----------+-------------
 dbid                    | oid                      |           |          |         | plain    | 
 userid                  | oid                      |           |          |         | plain    | 
 queryid                 | bigint                   |           |          |         | plain    | 
 planid                  | bigint                   |           |          |         | plain    | 
 first_used              | timestamp with time zone |           |          |         | plain    | 
 last_used               | timestamp with time zone |           |          |         | plain    | 
 hints                   | text                     |           |          |         | extended | 
 avg_exec_time           | double precision         |           |          |         | plain    | 
 avg_est_cost            | double precision         |           |          |         | plain    | 
 min_avg_exec_time       | double precision         |           |          |         | plain    | 
 min_avg_est_cost        | double precision         |           |          |         | plain    | 
 plan_require_evaluation | text                     |           |          |         | extended | 
 plan_min_exec_time      | text                     |           |          |         | extended | 
View definition:
 WITH cte AS (
         SELECT yb_pg_stat_plans.dbid,
            yb_pg_stat_plans.userid,
            yb_pg_stat_plans.queryid,
            yb_pg_stat_plans.planid,
            yb_pg_stat_plans.first_used,
            yb_pg_stat_plans.last_used,
            yb_pg_stat_plans.hints,
            yb_pg_stat_plans.avg_exec_time,
            yb_pg_stat_plans.avg_est_cost,
            min(yb_pg_stat_plans.avg_exec_time) OVER (PARTITION BY yb_pg_stat_plans.dbid, yb_pg_stat_plans.userid, yb_pg_stat_plans.queryid) AS min_avg_exec_time,
            min(yb_pg_stat_plans.avg_est_cost) OVER (PARTITION BY yb_pg_stat_plans.dbid, yb_pg_stat_plans.userid, yb_pg_stat_plans.queryid) AS min_avg_est_cost
           FROM yb_pg_stat_plans
        )
 SELECT cte.dbid,
    cte.userid,
    cte.queryid,
    cte.planid,
    cte.first_used,
    cte.last_used,
    cte.hints,
    cte.avg_exec_time,
    cte.avg_est_cost,
    cte.min_avg_exec_time,
    cte.min_avg_est_cost,
        CASE
            WHEN cte.avg_exec_time = cte.min_avg_exec_time AND cte.min_avg_est_cost <> cte.avg_est_cost OR cte.avg_exec_time <> cte.min_avg_exec_time AND cte.min_avg_est_cost = cte.avg_est_cost THEN 'Yes'::text
            ELSE 'No'::text
        END AS plan_require_evaluation,
        CASE
            WHEN cte.avg_exec_time = cte.min_avg_exec_time THEN 'Yes'::text
            ELSE 'No'::text
        END AS plan_min_exec_time
   FROM cte
  ORDER BY cte.queryid, cte.planid, cte.last_used;
```

## Learn more

- Refer to [Get query statistics using pg_stat_statements](../pg-stat-statements/) to track planning and execution of all the SQL statements.
- Refer to [View live queries with pg_stat_activity](../../../../explore/observability/pg-stat-activity/) to analyze live queries.
- Refer to [View COPY progress with pg_stat_progress_copy](../../../../explore/observability/pg-stat-progress-copy/) to track the COPY operation status.
- Refer to [Analyze queries with EXPLAIN](../explain-analyze/) to optimize YSQL's EXPLAIN and EXPLAIN ANALYZE queries.

---
title: Query plan management
linkTitle: Query plan management
description: Capture all unique plans for a query so that you can track plan changes
headerTitle: Query plan management
headcontent: Monitor plan changes
menu:
  stable:
    identifier: query-plan-manage
    parent: query-tuning
    weight: 600
tags:
  feature: early-access
type: docs
---

Enterprises need stable and consistent performance during various system changes on YugabyteDB databases. One of the major causes of response time variability is query plan instability. Various factors can unexpectedly change the execution plan of queries. For example:

- Change in optimizer statistics (manually or automatically).
- Changes to the query planner configuration parameters.
- Changes to the schema, such as adding a new index.
- Changes to the bind variables used in the query.
- Minor version or major version upgrades to the database version.

Query plan management (QPM) serves two main objectives:

- Plan stability. QPM records the plan history and generated hints needed to restore a known good plan when changes occur in the system.
- Plan adaptability. QPM records new plans and their execution statistics so that you can evaluate when to adopt a better plan.

Because plans can change over time (you turn on the cost-based optimizer, for example), there is a chance a new plan for a query degrades performance. QPM provides views that capture all unique plans for a query so that you can look back and see when a plan changed. The entry for the old plan includes hints that can generate the same plan. You can then insert those hints into the hint plan table and get the old better-performing plan back.

QPM features are available in v2025.2.3 and later.

## Prerequisites

QPM is available in v2025.2.3.0 and later. While in Early Access, QPM is disabled by default.

To enable QPM, you set the `yb_pg_stat_plans_track` configuration parameter to `top` or `all`. For more information on configuring QPM, see [Configure QPM](#configure-qpm).

## Using QPM

You view QPM data using two views:

- [yb_pg_stat_plans](#yb-pg-stat-plans)
- [yb_pg_stat_plans_insights](#yb-pg-stat-plans-insights)

If you notice a query is performing poorly, you can run [EXPLAIN](../../../../api/ysql/the-sql-language/statements/perf_explain/) to get its current query ID and/or plan ID, and then use the query ID to look up the query in the yb_pg_stat_plans view to see if this is a new plan, or if its execution time has recently increased. To have EXPLAIN include the query and plan IDs, use the QUERYID and PLANID options. For example:

```sql
EXPLAIN (queryid on, planid on) SELECT count(*) FROM t0, t1 WHERE a0=a1;
```

Currently, only SELECT, INSERT, UPDATE, MERGE, DELETE, and EXECUTE statements for which hints can be generated are tracked. EXPLAIN ANALYZE statements are not tracked.

If hints cannot be generated for a plan, then the plan is not stored. (For example, `SELECT 1`, or `SELECT * FROM func()`.)

Nested queries and queries referencing catalog tables may or may not be tracked, depending on the settings of the `yb_pg_stat_plans_track` and `yb_pg_stat_plans_track_catalog_queries` parameters.

To prevent the planner from storing plans for a query in QPM, add the string `__YB_STAT_PLANS_SKIP` in an SQL comment of the statement. This provides a way to prevent QPM entries for selected queries.

A query may have multiple plans, such as when optimizer statistics change, or a new execution method becomes available.

For example, running the following two queries results in two entries in the QPM table, one for each plan:

```sql
/*+ MergeJoin(t0 t3) */ SELECT 1 FROM t0, t3 WHERE pk0_0 = pk3_0;

/*+ HashJoin(t0 t3) */ SELECT 1 FROM t0, t3 WHERE pk0_0 = pk3_0;
```

Multiple query IDs can also have the same plan:

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
EXPLAIN (queryid on, planid on) SELECT count(*) FROM t1, t0 WHERE a0=a1;
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
EXPLAIN (queryid on, planid on) SELECT count(*) FROM t0, t1 WHERE a1=a0;
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
EXPLAIN (queryid on, planid on) SELECT count(*) FROM t1, t0 WHERE a1=a0;
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

Although these are the same query (that is, they are logically equivalent and return the same set of rows, just written differently), they are assigned different Query IDs. This is because query ID assignment is sensitive to the following:

- The order of tables in the FROM clause: `FROM t1, t0` is not considered to be equivalent to `FROM t0, t1`.
- The order of clauses in predicates (conditions): `a1=a0` is not considered to be equivalent to `a0=a1`, or `a1=a2 AND b2=1` and `b2=1 AND a2=a1`.

These queries do however get the same plan, and the Plan IDs will be the same. Plan ID _does not_ care about the order of:

- FROM list items;
- clauses of ANDs; or
- a1 and a2 in a1=a2


### Pin a plan using QPM

To force a particular execution plan for a query, you "pin" the plan. You can use QPM to:

- Detect the plan change/regression
- Revert to a previous (good) plan
- Optionally, pin other known good plans to prevent any future regressions

The following example shows how you can use QPM features to pin a plan.

Assume a simple order and `order_details` schema with an initial state of 1000 accounts each with 20 orders. The join query uses a batched nested loop join as expected.

Then suppose a lot of accounts are added, each with a single order. After statistics are refreshed, the optimizer estimates about one order per account and may switch to a regular nested loop join even for the original accounts with 20 orders each. This would be considered a plan regression for those accounts.

#### Setup

1. Create the tables and add orders:

    ```sql
    CREATE TABLE orders (
        account_id INT,
        order_no INT,
        order_id TEXT,
        PRIMARY KEY (account_id, order_no));
    CREATE TABLE order_details (
        order_id TEXT PRIMARY KEY,
        details json);
    ```

1. Insert some data.

    ```sql
    INSERT INTO orders (account_id, order_no, order_id)
    SELECT a,o, a::text || '_' || o::text FROM generate_series(1,1000) a, generate_series(1,20) o;
    INSERT INTO order_details (order_id, details) SELECT order_id, '{}' from orders;
    ANALYZE orders;
    ANALYZE order_details;
    ```

1. Set the necessary parameters to enable QPM, and delete all entries from pg_stat_statements and the QPM table.

    ```sql
    CREATE EXTENSION IF NOT EXISTS pg_stat_statements;
    SET yb_enable_cbo = on;
    SET yb_pg_stat_plans_track = 'all';
    SELECT pg_stat_statements_reset();
    SELECT yb_pg_stat_plans_reset(NULL, NULL, NULL, NULL);
    \pset pager off
    \timing
    ```

#### Inspect the plan

1. Use EXPLAIN ANALYZE to inspect the plan, then run the SELECT repeatedly to populate pg_stat_statements and QPM statistics:

    ```sql
    EXPLAIN ANALYZE SELECT d.details FROM orders o JOIN order_details d ON o.order_id = d.order_id WHERE account_id = 10;
    SELECT pg_stat_statements_reset();
    SELECT 'SELECT d.details FROM orders o JOIN order_details d ON o.order_id = d.order_id WHERE account_id = 10;' FROM generate_series(1,100); \gexec
    SELECT queryid, mean_exec_time, query, calls FROM pg_stat_statements
        WHERE query LIKE 'SELECT d.details FROM orders o JOIN order_details%';
    ```

1. Examine the QPM entries.

    ```sql
    SELECT s.queryid, s.query, p.planid, p.first_used, p.last_used,
        p.avg_exec_time, p.avg_est_cost, p.plan, p.calls FROM
        pg_stat_statements s JOIN yb_pg_stat_plans p ON s.queryid = p.queryid /* __YB_STAT_PLANS_SKIP */;
    ```

    You should see a BNL plan that was executed 100 times.

#### Add new orders

1. Load new data (many small accounts with just 1 order each):

    ```sql
    INSERT INTO orders (account_id, order_no, order_id)
    SELECT a, 1, a::text || '_1' FROM generate_series(1001,1000000) a;
    INSERT INTO order_details (order_id, details)
    SELECT order_id, '{}' from orders WHERE account_id > 1000 /* __YB_STAT_PLANS_SKIP */;
    ANALYZE orders;
    ANALYZE order_details;
    ```

1. Examine the plan:

    ```sql
    EXPLAIN ANALYZE SELECT d.details FROM orders o JOIN order_details d ON o.order_id = d.order_id WHERE account_id = 10;
    ```

    The plan now uses nested loop, and performance is worse.

1. Re-run the query and workload:

    ```sql
    SELECT pg_stat_statements_reset();
    SELECT 'SELECT d.details FROM orders o JOIN order_details d ON o.order_id = d.order_id WHERE account_id = 10;' FROM generate_series(1,100); \gexec
    ```

    Now there should be 200 executions of the query.

1. Run the following:

    ```sql
    SELECT queryid, mean_exec_time, query from pg_stat_statements
        WHERE query LIKE 'SELECT d.details FROM orders o JOIN order_details%' /* __YB_STAT_PLANS_SKIP */;
    ```

    Now there should be 2 plans, each with 100 executions.

#### Pin the plan

1. Get the plans used and their statistics (average execution time and first/last used time):

    ```sql
    SELECT s.queryid, s.query, p.planid, p.first_used, p.last_used,
        p.avg_exec_time, p.avg_est_cost, p.plan FROM
        yb_pg_stat_plans p LEFT JOIN pg_stat_statements s ON s.queryid = p.queryid
        WHERE s.query LIKE 'SELECT d.details FROM orders o JOIN order_details%' /* __YB_STAT_PLANS_SKIP */;
    ```

1. Get hints for the plans:

    ```sql
    SELECT s.queryid, s.query, p.planid, p.first_used, p.hints FROM
        yb_pg_stat_plans p LEFT JOIN pg_stat_statements s ON s.queryid = p.queryid
        WHERE s.query LIKE 'SELECT d.details FROM orders o JOIN order_details%' /* __YB_STAT_PLANS_SKIP */;
    ```

1. Inspect the current plan before pinning:

    ```sql
    EXPLAIN ANALYZE SELECT d.details FROM orders o JOIN order_details d ON o.order_id = d.order_id WHERE account_id = 10;
    ```

1. Enable and set up the hint table:

    ```sql
    CREATE EXTENSION IF NOT EXISTS pg_hint_plan;
    SET pg_hint_plan.enable_hint_table = on;
    SET pg_hint_plan.yb_use_query_id_for_hinting = on;
    ```

1. Remove all hints from hint table:

    ```sql
    DELETE FROM hint_plan.hints;
    ```

1. Insert the hints for the earliest plan into the hint table:

    ```sql
    INSERT INTO hint_plan.hints (norm_query_string, application_name, hints)
    SELECT s.queryid::text, '', substring(p.hints from 5 for char_length(p.hints) - 7) FROM
        pg_stat_statements s JOIN yb_pg_stat_plans p ON s.queryid = p.queryid
        WHERE s.query LIKE 'SELECT d.details FROM orders o JOIN order_details%'
        ORDER BY first_used ASC LIMIT 1
    ON CONFLICT (norm_query_string, application_name) DO UPDATE
        SET hints = EXCLUDED.hints;
    ```

1. Verify the hints are in the hint table.

    ```sql
    SELECT * from hint_plan.hints; 
    ```

1. Check that EXPLAIN plan is updated:

    ```sql
    EXPLAIN (ANALYZE, HINTS) SELECT d.details FROM orders o JOIN
        order_details d ON o.order_id = d.order_id WHERE account_id = 10;
    ```

1. Reset the statistics and re-run the workload:

    ```sql
    SELECT yb_pg_stat_plans_reset(null, null, null, null);
    SELECT 'SELECT d.details FROM orders o JOIN order_details d ON o.order_id = d.order_id WHERE account_id = 10;' FROM generate_series(1,100); \gexec
    SELECT s.queryid, s.query, p.planid, p.first_used, p.last_used,
        p.avg_exec_time, p.avg_est_cost, p.plan, p.calls FROM
        pg_stat_statements s JOIN yb_pg_stat_plans p ON s.queryid = p.queryid /* __YB_STAT_PLANS_SKIP */;
    ```

## Configure QPM

Use the following [YSQL configuration parameters](../../../../reference/configuration/yb-tserver/#ysql-configuration-parameters) to configure QPM and how the yb_pg_stat_plans view is populated.

| Option | Description | Default |
| :----- | :---------- | :------ |
| `yb_pg_stat_plans_track` | Turns tracking on or off. Valid values are:<ul><li>none: no plans are tracked, disables QPM.</li><li>top: track only top-level (non-nested) statements.</li><li>all: track all statements.</li></ul> | none |
| `yb_pg_stat_plans_max_cache_size` | Specifies the Maximum number of entries to store (1-50000). | 5000 |
| `yb_pg_stat_plans_cache_replacement_algorithm` | Controls the eviction policy used by QPM once the number of entries reaches yb_pg_stat_plans_max_cache_size. Valid values are:<ul><li>`simple_clock_lru` - uses an efficient simulation of clock-based Least Recently Used (LRU) algorithm to find the entry to evict.</li><li>`true_lru` - evicts the entry with the oldest last_used value (less efficient but finds the actual oldest entry).</li></ul> Requires restart. | simple_clock_lru |
| `yb_pg_stat_plans_track_catalog_queries` | Controls tracking of statements referencing catalog tables. | true |
| `yb_pg_stat_plans_verbose_plans` | Set to `true` to generate and store verbose plans. | false |
| `yb_pg_stat_plans_plan_format` | Controls text format for plans.<br/>Valid values are `text`, `json`, `yaml`, and `xml`. | json |

The yb_pg_stat_plans data is stored per node; all backends on a specific node read and write to the same table. The data is persisted across restarts.

The table is fixed size (default 5000 unique pairs), set using `yb_pg_stat_plans_max_cache_size`.

When the limit on the number of entries is reached, the oldest entry is dropped using a replacement strategy, set using `yb_pg_stat_plans_cache_replacement_algorithm`.

If the hint or plan text does not fit after compression, then the text payload is truncated. Truncated hints cannot be used to pin the plan.

### Reset QPM entries

Use the `yb_pg_stat_plans_reset(dbid OID, userid OID, queryid BIGINT, planid BIGINT)` function to manually remove entries from the table.

- If dbid is NULL, the function uses the current database.
- If userid is NULL, the function uses the current user.
- If queryid is NULL, the function removes all entries matching planid, or all entries if planid is also NULL.
- If planid is NULL, removes all entries matching queryid, or all entries if queryid is also NULL.

The function returns the number of entries removed.

## Views

### yb_pg_stat_plans

The yb_pg_stat_plans view serves as the primary method for accessing QPM data.

You can retrieve all QPM data using the following query:

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

The columns of the yb_pg_stat_plans view are described, along with their purpose, in the following table.

| Column | Purpose | Description |
| :----- | :------ | :---------- |
| dbid   | Identify a plan | OID of database in which the statement was executed. |
| userid |         | OID of user who executed the statement. |
| queryid |        | Hash code to identify identical normalized queries. |
| planid |         | Hash of a query's execution plan representation. |
| plan   |         | Text representation of the plan. |
| first_used | Detect plan changes | First recorded instance of the [dbid query id, plan id] use. |
| last_used |         | Last recorded instance of the [dbid query id, plan id] use. |
| calls | Detect plan regressions | Number of times [dbid query id, plan id] pair is used. |
| avg_exec_time |         | Average execution time. |
| max_exec_time |         | Maximum recorded execution time for this plan. |
| max_exec_time_params |         | This particular set of query parameters led to the longest execution time. |
| avg_est_cost |         | Planner's average estimated cost for the plan. |
| hints | Pin the plan | These hints, if applied during query planning, would lead to the same plan being used. |

yb_pg_stat_plans does not store query text. You can retrieve query text by joining with pg_stat_statements on `queryid`. For example:

```sql
SELECT CASE WHEN ss.query IS NOT NULL THEN ss.query ELSE '<NULL>' END as query_string, hints, … 
FROM yb_pg_stat_plans qpm LEFT JOIN pg_stat_statements ss ON qpm.queryid=ss.queryid;
```

### yb_pg_stat_plans_insights

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
            WHEN (cte.avg_exec_time = cte.min_avg_exec_time AND cte.min_avg_est_cost <> cte.avg_est_cost) OR (cte.avg_exec_time <> cte.min_avg_exec_time AND cte.min_avg_est_cost = cte.avg_est_cost) THEN 'Yes'::text
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

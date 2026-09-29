---
title: Enable a parallel index scan for a temporal join
headerTitle: Enable a parallel index scan for a temporal join
linkTitle: Temporal join scans
description: Which parallel scan parameter matches the index being scanned, and how to recognize the plan in EXPLAIN.
menu:
  stable:
    identifier: query-tuning-parallel-temporal-join
    parent: query-tuning
    weight: 520
type: docs
---

[YSQL](../../../../api/ysql/) can use [PostgreSQL parallel query](https://www.postgresql.org/docs/15/parallel-query.html) for a temporal join used in analytics. The planner can choose a parallel index scan on the temporal table and a [batched nested loop join](../../../../architecture/query-layer/join-strategies/#batched-nested-loop-join-bnl) for primary key lookups into the joined table. You can keep the original schema and SQL.

The join has a range predicate on one side and a primary key lookup on the other.

{{<lead link="../../../../explore/ysql-language-features/advanced-features/parallel-index-scan/">}}
For a worked schema and query, see [Parallel index scans for temporal joins](../../../../explore/ysql-language-features/advanced-features/parallel-index-scan/).
{{</lead>}}

## When the planner chooses this plan

This plan is most useful for:

- large time-window analytics over range-indexed columns
- HTAP-style reads against live data
- temporal joins where one side is filtered by a range predicate and the other side is joined by primary key

It is usually less helpful for very small time ranges, point lookups, and systems already saturated on CPU, where extra workers may add contention.

The planner can parallelize the scan when the leading index column matches the range predicate that drives it. An index that begins with `tt_to` can drive a filter on `tt_to > ... AND tt_to <= ...`. The plan then includes Gather or Gather Merge, worker processes, and a parallel index scan on that index.

In `EXPLAIN`, the indicator is `Parallel Index Scan` on the temporal index, inside a batched nested loop whose inner side is an index scan of the joined table's primary key. A serial index scan or a parallel sequential scan is a different plan.

In testing for this query shape, the parallel index scan path is approximately 50% to 55% faster than the corresponding non-parallel plan. Gains typically improve as the amount of qualifying work increases.

YugabyteDB also provides tablet-level parallelism independently of PostgreSQL parallel query, so some workloads benefit from both worker-based parallel execution and distributed tablet-level parallelism.

## Enable the plan

Enable the [cost-based optimizer](../../../../best-practices-operations/ysql-yb-enable-cbo/) and [parallel query](../../../../additional-features/parallel-query/).

The parameter must match the sharding of the index being scanned. For this pattern that index is usually [range-sharded](../../../../architecture/docdb-sharding/sharding/#range-sharding), so set `yb_enable_parallel_scan_range_sharded`.

- Colocated indexes: `yb_enable_parallel_scan_colocated`. This parameter was added in v2025.2.2 and defaults to `true`.
- {{<tags/feature/ea idea="1516">}}[Hash-sharded](../../../../architecture/docdb-sharding/sharding/#hash-sharding) indexes: `yb_enable_parallel_scan_hash_sharded`. [Range-sharded](../../../../architecture/docdb-sharding/sharding/#range-sharding) indexes: `yb_enable_parallel_scan_range_sharded`. These parameters are available in v2025.2.3 and later and default to `false`.

Set the session degree of parallelism with `max_parallel_workers_per_gather`. `parallel_tuple_cost` and `parallel_setup_cost` can stay at their defaults. With those costs, the planner chooses a parallel plan when the workload is large enough to justify the overhead.

## Previous workaround

Before this plan was available, a common approach was to create bucketized indexes, expose them through `UNION ALL` views, and rely on Parallel Append. That approach can still work, and it adds schema and query complexity. For temporal joins that match this pattern, a parallel index scan uses standard PostgreSQL indexes and SQL.

## Best practices

To improve the chances of getting a parallel index scan for temporal joins:

- create an index whose leading column matches the temporal range predicate
- enable the cost-based optimizer and the parallel scan parameter that matches the sharding of the index being scanned. For this pattern that index is usually range-sharded (`yb_enable_parallel_scan_range_sharded`)
- use a time window or result set large enough that parallelism is cost-effective
- verify the plan with `EXPLAIN (ANALYZE, DIST)`

---
title: Parallel index scans for temporal joins
linkTitle: Parallel index scans
description: Use native PostgreSQL Parallel Query in YSQL to run parallel index scans for temporal joins, without bucketized indexes or UNION ALL rewrites.
menu:
  stable:
    identifier: parallel-index-scan-temporal-joins
    parent: advanced-features
    weight: 780
type: docs
---

YSQL supports native [PostgreSQL parallel queries](https://www.postgresql.org/docs/15/parallel-query.html) (PQ) for a common temporal join pattern used in analytics. Starting in v2025.2.2, the planner can choose a Parallel Index Scan on the temporal table and a YB Batched Nested Loop Join for primary-key lookups into the joined table.

This removes the need for [earlier workarounds](#previous-workaround) such as:

- bucketized indexes
- `UNION ALL` views
- query rewrites designed to force `Parallel Append`

With the current optimizer behavior, you can often keep your original schema and SQL and let YSQL parallelize the range scan directly.

## When to use this

This optimization is most useful for:

- large time-window analytics over range-indexed columns
- HTAP-style reads against live data
- temporal joins where one side is filtered by a range predicate and the other side is joined by primary key

It is usually less helpful for:

- very small time ranges
- point lookups
- systems already saturated on CPU, where extra workers may add contention

## Before you begin

This example assumes:

- YugabyteDB v2025.2.2
- YSQL [cost-based optimizer](../../../../best-practices-operations/ysql-yb-enable-cbo/) enabled

## Enable the required settings

To enable this behavior at the database level, set the following parameters:

```sql
SET yb_enable_cbo = on;
SET yb_enable_parallel_scan_colocated = on;
SET yb_enable_parallel_scan_range_sharded = on;
SET yb_enable_parallel_scan_hash_sharded = on;
SET yb_enable_parallel_append = on;
```

For this example, set the session-level DOP as follows:

```sql
SET max_parallel_workers_per_gather = 6;
```

All other parallel-cost settings can remain at their defaults, including:

- parallel_tuple_cost
- parallel_setup_cost

With default costs, the planner still assumes parallel execution has meaningful overhead, so it typically chooses a parallel plan only when the workload is large enough to justify it.

## Example schema

The following anonymized schema demonstrates a temporal join pattern where PQ can activate.

```sql
CREATE SCHEMA pq_anon_parallel_demo;
SET search_path TO pq_anon_parallel_demo;

CREATE TABLE entity_payload (
  version_ref BIGINT NOT NULL,
  entity_type_id INT NOT NULL,
  payload JSONB NOT NULL,
  PRIMARY KEY ((version_ref) HASH)
);

CREATE TABLE entity_validity (
  version_ref BIGINT NOT NULL,
  entity_ref TEXT NOT NULL,
  tt_from TIMESTAMPTZ NOT NULL,
  tt_to TIMESTAMPTZ NOT NULL,
  vt_from TIMESTAMPTZ NOT NULL,
  vt_to TIMESTAMPTZ NOT NULL,
  aux_metric INT NOT NULL,
  PRIMARY KEY ((version_ref) HASH)
);

CREATE INDEX idx_entity_validity_tt_to_asc_vkey
ON entity_validity (tt_to ASC, version_ref ASC);
```

## Example query

The following query counts rows in a temporal validity window and joins to the payload table by `version_ref`:

```sql
SELECT count(*)
FROM entity_validity v
JOIN entity_payload p ON p.version_ref = v.version_ref
WHERE v.tt_to > timestamptz '2025-11-17 06:06:09.391+00'
  AND v.tt_to <= timestamptz '2025-11-17 06:06:09.391+00' + interval '180 day'
  AND v.vt_to > timestamptz '2025-11-17 06:06:09.391+00'
  AND v.tt_from <= timestamptz '2025-11-17 06:06:09.391+00'
  AND v.vt_from <= timestamptz '2025-11-17 06:06:09.391+00'
  AND ((p.payload->>'hasNonFlatPosition')::boolean = true);
```

## Why this plan can parallelize

For this pattern, parallelization becomes possible when the leading index column matches the range predicate used to drive the scan.

In the example, the index begins with:

```sql
tt_to ASC
```

and the query filters on:

```sql
v.tt_to > ...
AND v.tt_to <= ...
```

This allows the planner to consider a plan that includes:

- Gather or Gather Merge
- launched worker processes
- Parallel Index Scan on the temporal index

## Verify that Parallel Query is being used

Run the following:

```sql
EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT count(*)
FROM entity_validity v
JOIN entity_payload p ON p.version_ref = v.version_ref
WHERE v.tt_to > timestamptz '2025-11-17 06:06:09.391+00'
  AND v.tt_to <= timestamptz '2025-11-17 06:06:09.391+00' + interval '180 day'
  AND v.vt_to > timestamptz '2025-11-17 06:06:09.391+00'
  AND v.tt_from <= timestamptz '2025-11-17 06:06:09.391+00'
  AND v.vt_from <= timestamptz '2025-11-17 06:06:09.391+00'
  AND ((p.payload->>'hasNonFlatPosition')::boolean = true);
```

Look for a plan shape similar to the following:

```output
Finalize Aggregate
  ->  Gather
        Workers Planned: 6
        Workers Launched: 6
        ->  Partial Aggregate
              ->  YB Batched Nested Loop Join
                    ->  Parallel Index Scan using idx_entity_validity_tt_to_asc_vkey on entity_validity v
                    ->  Index Scan using entity_payload_pkey on entity_payload p
```

The key indicator is:

```output
Parallel Index Scan using idx_entity_validity_tt_to_asc_vkey
```

If you see this node, the temporal side of the join is using a parallel index scan rather than a serial index scan or parallel sequential scan.

## Expected performance

In testing for this query shape, the parallel index scan path is approximately 50% to 55% faster than the corresponding non-PQ plan. Gains typically improve as the amount of qualifying work increases.

YugabyteDB also provides tablet-level parallelism independently of PostgreSQL PQ, so some workloads may benefit from both:

- PostgreSQL worker-based parallel execution
- YugabyteDB distributed tablet-level parallelism

## Previous workaround

Before this optimization path was available, a common approach was to:

- create bucketized indexes
- expose them through UNION ALL views
- rely on Parallel Append

That approach can still work, but it increases schema and query complexity. For temporal joins that match the pattern shown here, a native Parallel Index Scan lets you use standard PostgreSQL indexes and SQL instead.

## Best practices

To improve the chances of getting a parallel index scan for temporal joins:

- create an index whose leading column matches the temporal range predicate
- enable the YSQL cost-based optimizer and parallel scan GUCs
- use a sufficiently large time window or result set so parallelism is cost-effective
- verify the actual plan with EXPLAIN (ANALYZE, DIST)

## Learn more

- [Parallel queries](../../../../additional-features/parallel-query/)

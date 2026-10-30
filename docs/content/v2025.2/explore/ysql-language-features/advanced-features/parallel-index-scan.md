---
title: Parallel index scans for temporal joins
linkTitle: Parallel index scans
description: Worked example of a temporal join that uses a parallel index scan, including the schema, query, and EXPLAIN output.
menu:
  v2025.2:
    identifier: parallel-index-scan-temporal-joins
    parent: advanced-features
    weight: 780
type: docs
---

[YSQL](../../../../api/ysql/) supports native [PostgreSQL parallel queries](https://www.postgresql.org/docs/15/parallel-query.html) (PQ) for a common temporal join pattern used in analytics. This example counts rows in a time window on `entity_validity` and joins them to `entity_payload` by primary key. Starting in v2025.2.3, the planner can scan the range-sharded `tt_to` index in parallel and look up that primary key with a batched nested loop.

For more information about when the planner chooses that plan, see [Enable a parallel index scan for a temporal join](../../../../launch-and-manage/monitor-and-alert/query-tuning/parallel-temporal-join/).

## Before you begin

This example assumes:

- YugabyteDB v2025.2.3, because the parallel scan is of the [range-sharded](../../../../architecture/docdb-sharding/sharding/#range-sharding) index `idx_entity_validity_tt_to_asc_vkey` (`yb_enable_parallel_scan_range_sharded`)
- YSQL [cost-based optimizer](../../../../best-practices-operations/ysql-yb-enable-cbo/) enabled

## Enable the required settings

To enable this behavior at the database level, set the following parameters:

```sql
SET yb_enable_cbo = on;
SET yb_enable_parallel_scan_colocated = on;
SET yb_enable_parallel_scan_range_sharded = on;
SET yb_enable_parallel_scan_hash_sharded = on;
```

For this example, set the session-level degree of parallelism (DOP) as follows:

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

If you see this note, the temporal side of the join is using a parallel index scan. For when the planner chooses that plan, see [When the planner chooses this plan](../../../../launch-and-manage/monitor-and-alert/query-tuning/parallel-temporal-join/#when-the-planner-chooses-this-plan).

---
title: Query cluster-wide statistics with global views
headerTitle: Global views
linkTitle: Global views
description: Query per-node statistics views across every live tablet server from a single YSQL session.
headcontent: Query cluster-wide counterparts of per-node statistics views
menu:
  stable:
    identifier: global-views
    parent: explore-observability
    weight: 295
type: docs
tags:
  feature: tech-preview
---

YugabyteDB's statistics and monitoring views are per node. [`pg_stat_activity`](../pg-stat-activity/) shows the backends on the node you happen to be connected to, [`pg_stat_statements`](../../../launch-and-manage/monitor-and-alert/query-tuning/pg-stat-statements/) accumulates counters for queries that ran on that node, and [Active Session History](../active-session-history/) samples only that node's sessions. Answering a cluster-wide question ("which node is running the slowest query right now?", "how many total calls has this statement taken across the cluster?") has meant connecting to every node in turn and stitching the results together by hand.

Global views remove that step. Each per-node view has a cluster-wide counterpart named `gv$<view_name>`, in `pg_catalog`, that returns the union of that view's rows from every live YB-TServer. A `server_uuid` column on each row identifies which node it came from, so a single session can aggregate, filter, and join across the whole cluster:

```plpgsql
SELECT server_uuid, count(*) AS active_backends
FROM gv$pg_stat_activity
WHERE state = 'active'
GROUP BY server_uuid;
```

Global views are {{<tags/feature/tp idea="2134">}} and available in v2026.1.2 and later. They are read-only, and they are built on [postgres_fdw](../../../additional-features/pg-extensions/extension-postgres-fdw/), so you query them with ordinary SQL: WHERE, GROUP BY, ORDER BY, joins, and subqueries all work.

When you query a global view, the planner expands it into one scan per live YB-TServer, each node runs the corresponding local view, and the coordinating node unions the results.

## Configuration and usage

- How to [enable global views](../../../launch-and-manage/monitor-and-alert/global-views/#enable-global-views)
- See [Monitor with Global views](../../../launch-and-manage/monitor-and-alert/global-views/) for privileges, the default `gv$` views, user-defined views, and best practices
- Global views [limitations](../../../launch-and-manage/monitor-and-alert/global-views/#limitations)

## Examples

{{% explore-setup-single-new %}}

Because these views are accessible via YSQL, run the examples using [ysqlsh](../../../api/ysqlsh/#starting-ysqlsh). Enable the feature in the session first:

```plpgsql
SET yb_enable_global_views = on;
```

### Busiest node

```plpgsql
SELECT server_uuid, count(*) AS active
FROM gv$pg_stat_activity
WHERE state = 'active' AND backend_type = 'client backend'
GROUP BY server_uuid
ORDER BY active DESC;
```

### Cluster-wide top statements

Fold each statement's per-node counters together:

```plpgsql
SELECT query,
       sum(calls) AS calls,
       sum(total_exec_time) AS total_exec_time
FROM gv$pg_stat_statements
GROUP BY queryid, query
ORDER BY total_exec_time DESC
LIMIT 10;
```

This returns one row per statement, not one row per statement per node. `server_uuid` is not in the GROUP BY, so the per-node counters for a statement that ran on three nodes are summed into a single row. A statement's `queryid` is a hash of its normalized parse tree, including the catalog OIDs of the objects it touches, and YugabyteDB's catalog is cluster-wide, so the same statement carries the same `queryid` on every node. Add `server_uuid` to the SELECT and GROUP BY when you want the per-node breakdown instead.

### Wait-event distribution

Wait-event distribution across the cluster, from Active Session History:

```plpgsql
SELECT wait_event_component, wait_event, count(*) AS samples
FROM gv$yb_active_session_history
WHERE sample_time >= now() - interval '10 minutes'
GROUP BY wait_event_component, wait_event
ORDER BY samples DESC;
```

That `now()` predicate runs on the node you are connected to, not on the remote nodes, so every sample in every node's buffer crosses the network first. To push the time filter down, evaluate `now()` once and send the result as a constant. In ysqlsh, `\gset` captures it into a variable:

```sql
SELECT now() - interval '10 minutes' AS cutoff \gset

SELECT wait_event_component, wait_event, count(*) AS samples
FROM gv$yb_active_session_history
WHERE sample_time >= :'cutoff'
GROUP BY wait_event_component, wait_event
ORDER BY samples DESC;
```

`EXPLAIN (VERBOSE)` shows what this buys. With `now()`, each node is asked for the timestamp column so the filter can be applied locally:

```output
Filter: (sample_time >= (now() - '00:10:00'::interval))
Remote SQL: SELECT sample_time, wait_event FROM pg_catalog.yb_active_session_history_with_server_uuid
```

With the constant, the filter travels with the query and `sample_time` no longer has to come back at all:

```output
Remote Filter: (sample_time >= '2026-09-15 14:06:43.346362+05:30'::timestamp with time zone)
Remote SQL: SELECT wait_event FROM pg_catalog.yb_active_session_history_with_server_uuid WHERE ((sample_time >= '2026-09-15 14:06:43.346362+05:30'))
```

Outside ysqlsh, do the same thing in the application: compute the cutoff, then build the query with that value in it.

### Terminated queries

Queries that were terminated anywhere in the cluster:

```plpgsql
SELECT server_uuid, databasename, query_text, termination_reason
FROM gv$yb_terminated_queries
ORDER BY query_start_time DESC
LIMIT 20;
```

## Learn more

- [Monitor with Global views](../../../launch-and-manage/monitor-and-alert/global-views/)
- [Active Session History](../active-session-history/)
- [pg_stat_activity](../pg-stat-activity/)
- [pg_stat_statements](../../../launch-and-manage/monitor-and-alert/query-tuning/pg-stat-statements/)
- [postgres_fdw extension](../../../additional-features/pg-extensions/extension-postgres-fdw/)

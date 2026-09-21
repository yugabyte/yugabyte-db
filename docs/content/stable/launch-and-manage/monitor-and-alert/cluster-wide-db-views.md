---
title: Monitor with Cluster-wide database views
headerTitle: Monitor with Cluster-wide database views
linkTitle: Cluster-wide views
description: Enable and use cluster-wide database views to query per-node statistics across every live YB-TServer.
headcontent: Query cluster-wide counterparts of per-node statistics views
menu:
  stable:
    parent: monitor-and-alert
    identifier: cluster-wide-db-views-monitor
    weight: 125
type: docs
tags:
  feature: tech-preview
rightNav:
  hideH4: true
---

[Cluster-wide database views](../../../explore/observability/cluster-wide-db-views/) provide cluster-wide counterparts of per-node statistics views. Each `gv$<view_name>` foreign table in `pg_catalog` returns the union of that view's rows from every live YB-TServer, with a `server_uuid` column identifying the source node.

Cluster-wide database views are {{<tags/feature/tp idea="2134">}} and available in v2026.1.2 and later. They are read-only, and they are built on [postgres_fdw](../../../additional-features/pg-extensions/extension-postgres-fdw/), so you query them with ordinary SQL: WHERE, GROUP BY, ORDER BY, joins, and subqueries all work.

A cluster-wide database view is a foreign table over a YugabyteDB-specific postgres_fdw server (`yb_global_views_server`) whose `server_type` is `federatedYugabyteDB`. There is no external host. When you query `gv$pg_stat_activity`, the planner expands that foreign table into one scan per live YB-TServer and sends each node the corresponding local view (with a `server_uuid` column prepended). The coordinating node unions the results and returns them to you.

Remote queries run as a dedicated non-superuser role (`yb_global_views_user`) that is a member of `pg_read_all_stats`. That role cannot write data or signal other backends, so a cluster-wide database view cannot be used to send DML or `pg_terminate_backend` to remote nodes.

## Enable cluster-wide database views

Cluster-wide database views are behind the [`yb_enable_global_views`](../../../reference/configuration/yb-tserver/#yb-enable-global-views) configuration parameter, which defaults to `false`. The `gv$` views always exist in `pg_catalog`; querying one while the parameter is off fails:

```output
ERROR:  federatedYB queries are not supported
HINT:  Must enable the GUC yb_enable_global_views
```

To turn the feature on for the whole cluster, set it on every YB-TServer:

```sh
--ysql_pg_conf_csv=yb_enable_global_views=true
```

`yb_enable_global_views` is a SUSET parameter, so a superuser can also turn it on for a single session without restarting anything:

```plpgsql
SET yb_enable_global_views = on;
```

A superuser can turn it on for another role, which is the usual way to give a dedicated monitoring role access without enabling the feature cluster-wide:

```plpgsql
ALTER ROLE monitoring_user SET yb_enable_global_views = on;
```

Parameters set at the role or database level take effect on new sessions.

## Required privileges

Querying a cluster-wide database view requires membership in the `pg_read_all_stats` role. Superusers already qualify.

```plpgsql
GRANT pg_read_all_stats TO monitoring_user;
```

Without it:

```output
ERROR:  permission denied for federated YugabyteDB query
HINT:  Must be a member of the pg_read_all_stats role.
```

## Naming

For every supported per-node view `X`, YugabyteDB creates two objects in `pg_catalog`:

| Object | What it is |
| :----- | :--------- |
| `X_with_server_uuid` | A view over `X` with a leading `server_uuid` column, holding the UUID of the node it is read on. This is what each node executes. |
| `gv$X` | A foreign table with the same column list, over the `yb_global_views_server` foreign server. This is what you query. |

`gv$X` is a valid unquoted identifier, so no quoting is needed:

```plpgsql
SELECT * FROM gv$pg_stat_activity;
```

The `server_uuid` column is the first column of every cluster-wide database view and is not present in the underlying per-node view. Its value is what `yb_get_local_tserver_uuid()` returns on that node.

## Views created by default

The following cluster-wide database views are created by default:

| Cluster-wide view | Underlying view |
| :---------- | :-------------- |
| `gv$yb_active_session_history` | [yb_active_session_history](../../../explore/observability/active-session-history/) |
| `gv$pg_stat_statements` | [pg_stat_statements](../query-tuning/pg-stat-statements/) |
| `gv$pg_stat_activity` | [pg_stat_activity](../../../explore/observability/pg-stat-activity/) |
| `gv$yb_terminated_queries` | [yb_terminated_queries](../../../explore/observability/yb-pg-stat-get-queries/) |
| `gv$yb_pg_stat_plans` | [yb_pg_stat_plans](../query-tuning/query-plan-manage/#yb-pg-stat-plans) |
| `gv$yb_pg_stat_plans_insights` | [yb_pg_stat_plans_insights](../query-tuning/query-plan-manage/#yb-pg-stat-plans-insights) |
| `gv$pg_stat_database` | `pg_stat_database` |
| `gv$pg_stat_all_tables` | `pg_stat_all_tables` |
| `gv$pg_stat_user_tables` | `pg_stat_user_tables` |
| `gv$pg_stat_all_indexes` | `pg_stat_all_indexes` |
| `gv$pg_stat_user_indexes` | `pg_stat_user_indexes` |
| `gv$pg_stat_user_functions` | `pg_stat_user_functions` |
| `gv$pg_stat_progress_copy` | [pg_stat_progress_copy](../../../explore/observability/pg-stat-progress-copy/) |
| `gv$pg_stat_progress_create_index` | `pg_stat_progress_create_index` |
| `gv$pg_stat_progress_analyze` | `pg_stat_progress_analyze` |
| `gv$pg_stat_replication` | [pg_stat_replication](../../../additional-features/change-data-capture/using-logical-replication/monitor/#pg-stat-replication) |
| `gv$pg_stat_replication_slots` | `pg_stat_replication_slots` |

To list the views that a cluster actually has:

```plpgsql
SELECT relname AS view_name
FROM pg_class
WHERE relnamespace = 'pg_catalog'::regnamespace
  AND relkind = 'f'
  AND relname LIKE 'gv$%'
ORDER BY 1;
```

A cluster-wide database view whose source view comes from an extension disappears with the extension. For example, `DROP EXTENSION pg_stat_statements CASCADE` drops `gv$pg_stat_statements` and `pg_stat_statements_with_server_uuid` along with it.

## Best practices

- **Filter before you aggregate.**

    WHERE clauses on immutable expressions are pushed to each node, so they cut both remote work and network traffic. LIMIT does not, so `ORDER BY ... LIMIT 10` still fetches every matching row from every node.

- **Restrict the query to the nodes you care about with a `server_uuid` filter.**

    Equality and IN against literal UUIDs are applied while the query is planned, so the other nodes are never contacted at all:

    ```plpgsql
    SELECT * FROM gv$pg_stat_activity
    WHERE server_uuid = '19e489ef49db4d47afda3bb039ff970a';
    ```

    ```plpgsql
    SELECT * FROM gv$pg_stat_activity
    WHERE server_uuid IN ('19e489ef49db4d47afda3bb039ff970a',
                          'b0125150aeae4dcd83a4c87c29de743a');
    ```

    Other shapes still return the right answer, but the filter is applied after the rows arrive, so every node is queried. A UUID that reaches the query as a parameter is one of those. Prepared statements and PL/pgSQL functions produce exactly that, so inline the UUID when the pruning matters. EXPLAIN shows the difference: one Foreign Scan per node that survives the filter. {{<issue 32660>}} widens the set of shapes that prune.

- **Check for WARNING messages on results you intend to act on.**

     A skipped node and a node with nothing to report both contribute zero rows, and the warning is the only thing that tells them apart. The warnings reach the client as ordinary notice messages: ysqlsh prints them above the result set, and drivers expose them through whatever they use for server notices (`PQsetNoticeProcessor` in libpq, `Statement.getWarnings()` in JDBC). Application code that never looks at notices will not see them.

- **Aggregate `pg_stat_statements` by `queryid`, not by node.**

    The same statement carries a separate row and separate counters on every node it ran on.

## Additional configuration

| Flag | Details |
| :--- | :------ |
| [`remote_pg_query_execution_rpc_timeout_ms`](../../../reference/configuration/yb-tserver/#remote-pg-query-execution-rpc-timeout-ms) | Per-node timeout, in milliseconds, for the RPC that carries a cluster-wide database view's remote query. Runtime-modifiable. A node that exceeds it is skipped with a WARNING and the query returns rows from the remaining nodes.<br>Default: `15000` |

## Limitations

- **Results are not a consistent snapshot.** Each node is queried independently, so rows from different nodes reflect slightly different points in time.
- **A node that is down, unreachable, or slow to respond is skipped.** The query returns rows from the remaining nodes and raises a WARNING for each skipped node, rather than failing. A cluster-wide database view result covers the nodes that answered, not necessarily all of them.
- **Per-node results are capped by the RPC message size.** If a node's response exceeds `rpc_max_message_size`, the rows that fit are returned and a WARNING reports the truncation. Narrow the query with a WHERE clause rather than raising the limit. {{<issue 30843>}}
- **The list of nodes is captured when the query is planned.** A cached plan (a prepared statement, or a query inside a PL/pgSQL function) is not invalidated when nodes are added or removed, so after a scale-up or scale-down an already-prepared statement can query a stale set of nodes. Re-prepare the statement, or run `DISCARD PLANS`. {{<issue 30918>}}
- **Only WHERE clauses and ORDER BY are pushed to the remote nodes.** LIMIT, DISTINCT, GROUP BY, and joins are evaluated on the node you are connected to, after all matching rows have been fetched. Aggregate pushdown is tracked in {{<issue 32660>}}.
- **Only immutable expressions are pushed down.** A predicate containing a stable or volatile function, such as `now()`, is evaluated locally, so every row is fetched before the filter applies. Evaluate the function once and send the result as a constant instead; see the `\gset` example under [Cluster-wide database views examples](../../../explore/observability/cluster-wide-db-views/#wait-event-distribution). {{<issue 30964>}}
- **Each node scan opens a new connection on the remote node for the duration of the scan.** There is no connection pooling yet, so a cluster-wide database view query is more expensive than a local view query and is not meant for a hot path. {{<issue 30396>}}
- **Memory on the coordinating node grows with the node count when the plan uses MergeAppend**, which is what a pushed-down ORDER BY produces. Each per-node result is buffered whole, and a MergeAppend reads every child at once, so it holds one buffer per node; a plain Append reads one child at a time and peaks at one buffer. `gv$yb_active_session_history` is the view whose per-node result is large enough for this to matter. Narrow the query, or drop the ORDER BY and sort the result separately. {{<issue 32925>}}
- **Nodes are read one at a time.** The plan contains one scan per node and the executor runs them in sequence, so a cluster-wide database view query costs roughly the sum of the per-node times, not the slowest one. The cost grows with cluster size. {{<issue 30280>}}
- **In a multi-region deployment the cost is dominated by catalog reads against the master leader**, not by the RPC to the remote tablet server. Each per-node scan opens a fresh connection on the target node, and catalog lookups for a new backend go to the master leader, so a master leader in another region makes every node scan pay that round trip. A `server_uuid` filter drops nodes from the plan and avoids their round trips. {{<issue 31927>}}
- **Cluster-wide database views are read-only.** Remote queries cannot write data or signal other backends.

## Learn more

- Cluster-wide database views [examples](../../../explore/observability/cluster-wide-db-views/#examples)

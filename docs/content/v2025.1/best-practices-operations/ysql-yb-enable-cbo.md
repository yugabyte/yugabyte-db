---
title: Enable cost-based optimizer
headerTitle: Enable cost-based optimizer
linkTitle: YSQL cost-based optimizer
description: Create optimal execution plans for queries
headcontent: Create optimal execution plans for queries
tags:
  other: ysql
menu:
  v2025.1:
    identifier: ysql-yb-enable-cbo
    parent: best-practices-operations
    weight: 60
type: docs
---

Enable the YugabyteDB [cost-based optimizer (CBO)](../../architecture/query-layer/planner-optimizer/) to create optimal execution plans for queries, providing significant performance improvements both in single-primary and distributed PostgreSQL workloads. This feature reduces or eliminates the need to use hints or modify queries to optimize query execution.

The CBO is controlled using the `yb_enable_cbo` configuration parameter. The `yb_enable_cbo` parameter provides a heuristic-based optimizer mode that operates independently of table analysis. This allows you to continue using the system without unexpected plan changes during the transition to cost-based optimization, and to selectively enable this mode for specific connections if needed.

Note that `yb_enable_cbo` replaces the `yb_enable_optimizer_statistics` and `yb_enable_base_scans_cost_model` parameters, which will be deprecated and removed in a future release.

## Settings

The following table shows `yb_enable_cbo` settings and the equivalent using the replaced parameters.

| yb_enable_cbo | Old Settings | Use of Statistics |
| :--- | :--- | :--- |
| on | yb_enable_statistics: N/A<br>yb_enable_base_scans_cost_model: on | Yes |
| off | yb_enable_statistics: off<br>yb_enable_base_scans_cost_model: off | No (new behavior) |
| legacy_mode | yb_enable_statistics: off<br>yb_enable_base_scans_cost_model: off | Partial<sup>*</sup> |
| legacy_stats_mode | yb_enable_statistics: on<br> yb_enable_base_scans_cost_model: off | Yes |

<sup>*</sup> `legacy_mode` is the default. `legacy_mode` uses only reltuples for index scan nodes, and both reltuples and the column statistics for other operations such as joins, GROUP BY, and more.

## Recommended settings

| Scenario | Tables analyzed | Setting |
| :--- | :--- | :--- |
| New installation, migrating from another system | N/A | on |
| Using CBO. yb_enable_base_scans_cost_model = on | Yes | on |
| Using default settings (yb_enable_optimizer_statistics = off, yb_enable_base_scans_cost_model = off) | No | off |
| Using default settings (yb_enable_optimizer_statistics = off, yb_enable_base_scans_cost_model = off) | Yes | legacy_mode |
| yb_enable_optimizer_statistics = on,  yb_enable_base_scans_cost_model = off | Yes | legacy_stats_mode |

## Turning on CBO

When enabling the cost models, ensure that packed row for colocated tables is enabled by setting the `--ysql_enable_packed_row_for_colocated_table` flag to true.

### If the tables are not analyzed

1. Set `yb_enable_cbo` to off.

    This setting ensures that the heuristic-based optimizer mode does not use the statistics inconsistently while tables are being analyzed. It's important to ensure that each client connection reflects the updated setting after the parameter change.

    For example, restart the servers with the parameter specified via the `ysql_pg_conf_csv` TServer flag, or use the `ysql_yb_enable_cbo` flag.

1. Remove any existing flag setting (yb_enable_optimizer_statistics, yb_enable_base_scans_cost_model) from `ysql_pg_conf_csv` and elsewhere (ALTER DATABASE, ALTER ROLE, hints, SET command issued in the application connection, and so on).

1. Analyze the tables.

1. Set `yb_enable_cbo` to on.

1. Re-establish any existing application connections (if specifying the parameter via ALTER DATABASE command, for example) or perform a rolling restart of the servers if you are setting the TServer flag.

### If the tables are already analyzed

Set `yb_enable_cbo` to on, then re-establish any existing application connections (if specifying the parameter via ALTER DATABASE command, for example) or perform a rolling restart of the servers if you are setting the TServer flag.

## ANALYZE and Auto Analyze service

Similar to [PostgreSQL autovacuum](https://www.postgresql.org/docs/current/routine-vacuuming.html#AUTOVACUUM), the YugabyteDB Auto Analyze service {{<tags/feature/ea idea="590">}} automates the execution of ANALYZE commands for any table where rows have changed more than a configurable threshold for the table. This ensures table statistics are always up-to-date.

Even with the Auto Analyze service, for the CBO to create optimal execution plans, you should still run ANALYZE manually on user tables after data load, as well as in other circumstances.

For more information, refer to [Auto Analyze](../../explore/query-1-performance/auto-analyze/).

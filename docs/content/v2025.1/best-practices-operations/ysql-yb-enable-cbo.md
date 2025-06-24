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

Use of the CBO is recommended for all YSQL deployments.

You configure the CBO using the [yb_enable_cbo](../../reference/configuration/yb-tserver/#yb-enable-cbo) configuration parameter. The `yb_enable_cbo` parameter also provides a heuristic-based optimizer mode that operates independently of table analysis. This allows you to continue using the system without unexpected plan changes during the transition to cost-based optimization, and to selectively enable this mode for specific connections if needed.

Note that `yb_enable_cbo` replaces the [yb_enable_optimizer_statistics](../../reference/configuration/yb-tserver/#yb-enable-optimizer-statistics) and [yb_enable_base_scans_cost_model](../../reference/configuration/yb-tserver/#yb-enable-base-scans-cost-model) parameters, which will be deprecated and removed in a future release.

For more information, refer to:

- [How to modify configuration parameters](../../reference/configuration/yb-tserver/#how-to-modify-configuration-parameters)
- [YSQL configuration parameters](../../reference/configuration/yb-tserver/#how-to-modify-configuration-parameters)

## Prerequisites

When enabling the cost models, ensure that packed row for colocated tables is enabled (the default). See [Packed row flags](../../reference/configuration/yb-tserver/#packed-row-flags).

## New deployments

For new YSQL deployments, or when migrating from another system, enable the CBO by adding the flag to `ysql_pg_conf_csv` as follows:

```sh
--ysql_pg_conf_csv=yb_enable_cbo=on
```

Alternatively, set the following YB-TServer flag:

```sh
--ysql_yb_enable_cbo=on
```

## Existing deployments

For existing deployments, when upgrading to a version of YugabyteDB that supports the `yb_enable_cbo` parameter (v2025.1 and later), the parameter is set depending on the current settings of the `yb_enable_optimizer_statistics` and `yb_enable_base_scans_cost_model` parameters.

The following table shows `yb_enable_cbo` settings and the equivalent using the replaced parameters.

| Current Settings | yb_enable_cbo | Use of Statistics |
| :--- | :--- | :--- |
| yb_enable_statistics: N/A<br>yb_enable_base_scans_cost_model: on | on | Yes |
| yb_enable_statistics: off<br>yb_enable_base_scans_cost_model: off | legacy_mode | Partial<sup>*</sup> |
| yb_enable_statistics: on<br> yb_enable_base_scans_cost_model: off | legacy_stats_mode | Yes |
| N/A | off | No (new behavior) |

<sup>*</sup> `legacy_mode` is currently the default. `legacy_mode` uses only reltuples for index scan nodes, and both reltuples and the column statistics for other operations such as joins, GROUP BY, and more.

You should migrate existing deployments from using `legacy_mode` or `legacy_stats_mode` to either `on` (recommended) or, if you do not want to use CBO, `off`.

<!--## Recommended settings

| Scenario | Tables analyzed | Setting |
| :--- | :--- | :--- |
| New installation, migrating from another system | N/A | on |
| Using CBO. yb_enable_base_scans_cost_model = on | Yes | on |
| Using default settings (yb_enable_optimizer_statistics = off, yb_enable_base_scans_cost_model = off) | No | off |
| Using default settings (yb_enable_optimizer_statistics = off, yb_enable_base_scans_cost_model = off) | Yes | legacy_mode |
| yb_enable_optimizer_statistics = on,  yb_enable_base_scans_cost_model = off | Yes | legacy_stats_mode |
-->

### Turn on CBO safely

To ensure that running applications don't use unsuitable plans, use the following instructions to migrate an existing deployment to use CBO.

### If the tables are not analyzed

1. Set `yb_enable_cbo` to `off`.

    This setting ensures that the heuristic-based optimizer mode does not use the statistics inconsistently while tables are being analyzed. It's important to ensure that each client connection reflects the updated setting after the parameter change.

    For example, restart the servers with the parameter specified via the `ysql_pg_conf_csv` TServer flag, or via the `ysql_yb_enable_cbo` flag.

1. Remove any existing flag setting (`yb_enable_optimizer_statistics`, `yb_enable_base_scans_cost_model`) from `ysql_pg_conf_csv` and elsewhere (ALTER DATABASE, ALTER ROLE, hints, SET command issued in the application connection, and so on).

1. Analyze the tables.

1. Set `yb_enable_cbo` to `on`.

1. Re-establish any existing application connections (if specifying the parameter via ALTER DATABASE command, for example) or perform a rolling restart of the servers if you are setting the TServer flag.

### If the tables are already analyzed

Set `yb_enable_cbo` to `on`, then re-establish any existing application connections (if specifying the parameter via [ALTER DATABASE](../../api/ysql/the-sql-language/statements/ddl_alter_db/) command, for example) or perform a rolling restart of the servers if you are setting the TServer flag.

## ANALYZE and Auto Analyze service

Use the [YugabyteDB Auto Analyze service](../../explore/query-1-performance/auto-analyze/) {{<tags/feature/ea idea="590">}} to automate the execution of ANALYZE commands for any table where rows have changed more than a configurable threshold. This ensures table statistics are always up-to-date.

Even with the Auto Analyze service, for the CBO to create optimal execution plans, you should still run ANALYZE manually on user tables after data load, as well as in other circumstances.

For more information, refer to [Auto Analyze](../../explore/query-1-performance/auto-analyze/).

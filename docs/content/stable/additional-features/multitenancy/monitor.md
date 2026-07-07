---
title: Multitenancy observability
headerTitle: Observability
linkTitle: Observability
description: Monitor per-database CPU usage and throttling in YugabyteDB.
headcontent: Monitor per-database CPU usage and throttling
menu:
  stable:
    identifier: multitenancy-monitor
    parent: multitenancy
    weight: 20
type: docs
rightNav:
  hideH4: true
---

When multitenancy is enabled, the YB-TServer exposes per-database CPU metrics and dedicated web UI endpoints so that you can see how CPU is being consumed and throttled across tenants.

## Per-database metrics

The YB-TServer periodically scrapes the cgroup filesystem and exposes the following per-database counters. Each metric is tagged with a `database_name` label so that you can break down CPU usage by tenant.

| Metric | Description |
| :--- | :--- |
| `database_user_cpu_us` | Total user-space CPU time (microseconds) spent doing work for the database. |
| `database_system_cpu_us` | Total kernel (system) CPU time (microseconds) spent doing work for the database. |
| `database_throttled_us` | Total time (microseconds) that work for the database was throttled because it reached its CPU limit. |

A rising `database_throttled_us` for a database indicates that the database is hitting its per-database CPU cap. These metrics are available from the YB-TServer metrics endpoint alongside other server metrics; for more information on collecting metrics, see [Metrics](../../../launch-and-manage/monitor-and-alert/metrics/).

## Web UI endpoints

The YB-TServer web UI (by default on port 9000) provides two endpoints for inspecting the cgroup hierarchy and thread placement.

### /cgroups

The `/cgroups` endpoint shows the state of the cgroup hierarchy that the YB-TServer manages. For leaf cgroups, it displays the thread names and detailed CPU statistics, which is useful for understanding how threads and databases map to cgroups and where CPU is being consumed and throttled.

### /threadz

The `/threadz` endpoint lists all threads in the process. When multitenancy is enabled, the cgroup that each thread belongs to is shown, so you can confirm that database work, system work, and default (uncategorized) threads are assigned to the expected cgroups.

## Learn more

- [Metrics](../../../launch-and-manage/monitor-and-alert/metrics/)
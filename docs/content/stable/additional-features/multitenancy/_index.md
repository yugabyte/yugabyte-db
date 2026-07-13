---
title: Multitenancy CPU isolation
headerTitle: Multitenancy
linkTitle: Multitenancy
description: Isolate CPU usage across databases (tenants) on a YugabyteDB node.
headcontent: Prevent one database from starving others of CPU
menu:
  stable:
    identifier: multitenancy
    parent: additional-features
    weight: 45
type: indexpage
cascade:
  tags:
    feature: early-access
---

Multitenancy CPU isolation (also referred to as the resource governor) treats each YSQL database in a universe as a tenant, enforcing per-database CPU limits on each node. This provides predictable performance isolation across tenants, preventing noisy-neighbor problems and acting as insurance against rare incidents such as bugs and runaway workloads.

The feature is built on [Linux control groups (cgroups)](https://man7.org/linux/man-pages/man7/cgroups.7.html). When enabled, the YB-TServer creates and manages a cgroup hierarchy, assigns threads and thread pools that do work for a specific database to per-database cgroups, and lets the Linux scheduler enforce the configured CPU limits.

Because enforcement happens at the OS level, no database can bypass the scheduling policy.

| Scenario | Behavior |
| :--- | :--- |
| No contention | A database may consume nearly all available CPU if other databases are idle. |
| CPU contention | Active databases receive equal CPU weighting so no single database can monopolize the cluster. |
| Optional CPU cap | A configurable maximum CPU percentage limits every database to the same ceiling, even when idle CPU exists. |

For example, consider four applications sharing a 64 vCPU cluster. During normal operation, a single application may temporarily use most of the cluster if the others are idle. When all four become active simultaneously, the resource governor redistributes CPU fairly across the active databases. If you configure a 25% CPU cap, no database can consume more than approximately 16 vCPUs regardless of available spare capacity.

## How it works

Each tenant corresponds to one database, and each user database corresponds to a tenant. (The standard template databases are not tenants.)

The resource governor provides three related controls.

### Per-database maximum CPU

Every user database is given its own cgroup with an identical maximum CPU limit and equal scheduling weight. Threads that do work for a database (such as PostgreSQL backends, parallel workers, and the RPC threads servicing that database's requests) are placed in the corresponding cgroup. The Linux scheduler then caps the CPU that any single database can consume and shares CPU fairly when multiple databases compete for it.

The per-database maximum is set with the [qos_max_db_cpu_percent](../../reference/configuration/yb-tserver/#qos-max-db-cpu-percent) flag, expressed as a percentage of the node's non-system-reserved CPU.

### Per-database minimum CPU

A minimum per-database CPU guarantee can be enforced indirectly by capping the number of databases and weighting all per-database cgroups equally. If you allow at most _N_ databases, then under full contention each database is guaranteed at least `1/N` of the available (non-system) CPU.

For example, to guarantee each database at least 5% of CPU, cap the number of databases at `1 / 0.05 = 20`. The database count cap is set with the [qos_max_db_count](../../reference/configuration/yb-master/#qos-max-db-count) flag. When the cap is active, a `CREATE DATABASE` statement fails if it would exceed the limit.

### Reserved CPU for system work

To keep the cluster stable and responsive under heavy tenant load, a portion of CPU can be reserved for high-priority internal work (such as network reactors and Raft heartbeats) that all tenants depend on. This ensures critical background work is never fully starved by tenant queries.

The reserved amount is set with the [qos_system_high_cpu_reserved_percent](../../reference/configuration/yb-tserver/#qos-system-high-cpu-reserved-percent) flag.

## Scope and limitations

Keep the following in mind when planning a multitenant deployment:

- **YSQL only.** A cluster using multitenancy must not have any YCQL databases.
- **CPU only.** The feature limits CPU usage. It does not limit memory, disk I/O, or network.
- **Per node.** Limits apply to CPU usage on a single node (a single YB-TServer process), not to CPU aggregated across the cluster.
- **Linux only.** The feature relies on Linux cgroups and is not available on macOS.
- **cgroup version.** cgroup v2 is supported. (cgroup v1, used by older distributions such as AlmaLinux 8, is planned for a later release.)
- **Shared configuration.** All tenants share the same cluster, and therefore the same flag values. You cannot, for example, enable packed rows for one tenant and disable it for another.
- **No security isolation.** Multitenancy provides performance isolation only. It does not add any security or privacy isolation beyond the existing PostgreSQL security model.
- **Container support.** In containerized environments (Docker, Kubernetes), the cgroups filesystem is typically mounted read-only, so the container must be privileged with a writable, dedicated cgroup mount. For more information, see [Set up multitenancy](./set-up/#other-deployments).
- **Memory cgroup flag.** The feature is incompatible with the `postmaster_cgroup` flag (used to limit PostgreSQL backend memory), because CPU and memory isolation require different cgroup hierarchies.

## Learn more

{{<index/block>}}

  {{<index/item
    title="Set up multitenancy"
    body="Prepare cgroups and enable per-database CPU isolation."
    href="set-up/"
    icon="fa-thin fa-sliders">}}

  {{<index/item
    title="Observability"
    body="Monitor per-database CPU usage and throttling."
    href="monitor/"
    icon="fa-thin fa-chart-line">}}

{{</index/block>}}

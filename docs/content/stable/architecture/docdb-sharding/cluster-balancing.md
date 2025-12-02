---
title: Cluster balancing
headerTitle: Cluster balancing
linkTitle: Cluster balancing
description: Learn about cluster balancing mechanisms in YugabyteDB.
menu:
  stable:
    identifier: docdb-cluster-balancing
    parent: architecture-docdb-sharding
    weight: 1144
type: docs
---

## Overview

Cluster balancing is the process by which YugabyteDB automatically distributes data and queries across the nodes in a cluster to maintain fault tolerance and maximize performance. This page describes the various scenarios in which YugabyteDB performs cluster balancing and how to monitor its progress.

Cluster balancing is performed by a component of the YB-Master called the cluster balancer. The cluster balancer continuously monitors the cluster configuration and moves tablet data and leaders to evenly distribute data and query load, thus distributing CPU, disk, and network load across the cluster.

{{< note title="Terminology" >}}

Many of the flags and metrics on this page refer to the cluster balancer as the "load balancer". This document uses the more precise term "cluster balancer" to avoid confusion with network load balancers or client-side load balancers.

{{< /note >}}

### Disable cluster balancing

The cluster balancer can be disabled by setting the flag [`enable_load_balancing`](#enable-load-balancing) to `false` on YB-Master.

{{< warning title="Warning" >}}

Disabling the cluster balancer prevents automatic re-replication after node failure and can thus cause long-term durability issues. It is strongly recommended to not disable the cluster balancer.

{{< /warning >}}

## Background

### Placement policy

A placement policy is a specification of how data and queries should be distributed across the cluster. For example, a typical RF-3 placement might specify that there should be one replica of each tablet in 3 separate regions (for example, `us-west-1a`, `us-east-2a`, and `us-central-1a`), with leader preference towards `us-west-1a`.

The default placement policy is automatically inferred when a cluster is created, but it can be modified at the cluster-level with [`yb-admin`](../../../admin/yb-admin/), or overridden on a per-table basis with tablespaces (for YSQL clusters).

### Remote bootstrap

The cluster balancer creates new tablet replicas by a process called remote bootstrap. This involves copying data from a YB-TServer hosting an up-to-date tablet peer (typically, the geographically closest peer) to the destination YB-TServer.

### Blacklisting

A YB-TServer can be blacklisted or "leader blacklisted" to prevent it from hosting data or leaders, respectively. Blacklists are used to facilitate graceful shutdowns (for example, when performing staged restarts or OS patching on a node).

## Cluster balancing scenarios

Cluster balancing automatically occurs when a cluster is scaled in or out, when there are node outages (e.g., a failed node, or a network partition), and after creating or deleting tables and tablets.

### Cluster balancing when scaling

#### Scaling out

When scaling out (adding nodes), the cluster balancer will gracefully move tablet data and leaders to the new nodes, without any application impact. Tablet leaders handle queries, so moving leaders is equivalent to moving where the queries execute.

#### Scaling in

When scaling in, YugabyteDB Anywhere (YBA) will mark the nodes being removed as "blacklisted". This signals to the cluster balancer that it should:

- Gracefully transfer leadership off of the node being removed
- Re-replicate the data that was on the node to other nodes in the same region

Once all data and queries have moved off of the node, the node can be removed safely.

### Cluster balancing after node outages

On an unplanned node outage, cluster balancing occurs twice: once when the node goes down, and once if or when it recovers.

#### When the node goes down

- Tablet leadership is automatically transferred to healthy nodes within 3 seconds
- If the node is down for a prolonged time (15 minutes, by default) the cluster balancer will start creating new replicas of the data on the failed node on other nodes in the same region (if any exist)
- Queries will succeed as soon as tablet leaders move off of the failed node. The data balancing phase does not impact the availability of the database

#### When the failed node recovers

If or when the failed node recovers, the cluster balancer will rebalance tablet leaders onto the recovered node, as well as tablet data (if data had moved off of the node).

### Table and tablet creation and deletion

Operations that create or delete tablets may also trigger cluster balancing. Some examples of operations that may trigger cluster balancing are:

- Creating a table
- Dropping a table
- [Tablet splitting](../tablet-splitting/)

## Monitoring cluster balancer progress

{{< note title="Version availability" >}}

Some of the views below are only available in recent versions of YugabyteDB (release 2024.2.6.0-b46 and later, 2025.1.1.0-b197 and later, and all 2025.2 releases and later).

{{< /note >}}

While leader rebalancing is fast, data rebalancing can take anywhere from seconds to hours. The major factors influencing how long data rebalancing takes are:

- The size of data on the failed node
- The configured network and disk throughput
- Cluster balancer configuration flags (described in the [GFlags section](#gflags) below)

### Monitoring progress from the YB-Master UI

The YB-Master UI (found on port 7000, by default) contains two helpful views for monitoring cluster balancer progress.

#### Tablet Servers view

The "Tablet Servers" view is accessed by clicking on "Tablet Servers" on the left-side of the page. This view contains information about how many peers and leaders are hosted on each tablet server, as well as whether each tablet server is currently data or leader blacklisted.

#### Cluster Balancer view

The "Cluster Balancer" view is accessed by clicking on **Utilities → Load Balancer**. This page displays information about:

- Ongoing cluster balancing tasks (e.g., tablet leadership movement, tablets being added or removed)
- Any warnings during cluster balancing (e.g., a placement policy being unsatisfiable because there are no tservers in the requested zone)
- Ongoing remote bootstraps from one tablet server to another, including the speed of data transfer

### Monitoring progress from YugabyteDB Anywhere

The YugabyteDB Anywhere Metrics page also has some helpful graphs for monitoring cluster balancing progress.

#### Data Disk Usage graph

The "Data Disk Usage" graph displays the amount of disk space used by the selected tablet server. It can be found on the YBA UI's Metrics page, under the "Resource" tab. The disk usage should be similar on all tablet servers once data is balanced.

#### Cluster load balancer statistics graph

In recent versions (see [version availability note](#monitoring-cluster-balancer-progress) above), the "Cluster load balancer statistics" graph on the Metrics page's "Master Server" tab displays an estimate of the number of tablets that still have to be moved before the cluster is considered balanced.

Cluster balancing is complete when this graph reaches 0.

### Monitoring progress from metrics

The YBA graph above shows the following metrics, which can be used to track the number of tablet moves that are required before the cluster is considered balanced:

- `total_table_load_difference`
- `blacklisted_leaders`
- `tablets_in_wrong_placement`

The metric `estimated_data_to_balance_bytes` tracks how much data (in bytes) must be transferred before a cluster is balanced. This is often the best indicator of how long cluster balancing will take, as many clusters are disk or network limited.

## Configuration and tuning

In recent versions (2025.1.1.0-b144 and later), the only flag that users will typically need to set is [`remote_bootstrap_rate_limit_bytes_per_sec`](#remote-bootstrap-rate-limit-bytes-per-sec), which controls how much data each node will send or receive each second. This flag should be set according to the user's network and disk provisioning limits. Users should take into account the requirements of their workload.

For example, a user who provisioned their network and disk to 1 GiB/s, and whose workload typically uses 500 MiB/s of network and disk might want to set `remote_bootstrap_rate_limit_bytes_per_sec` to 300 MiB/s to leave some headroom (200 MiB/s) for workload spikes. Alternatively, they might set it to 500 MiB/s if they are interested in minimizing the time taken for node recovery and cluster scaling operations (at the cost of some workload impact).

{{< warning title="Important" >}}

If you upgrade to a version after 2025.1.1.0-b144, you must unset the older flags (listed in the [Legacy flags](#legacy-flags) section below) for YugabyteDB to use the faster cluster balancing defaults.

{{< /warning >}}

In older releases, there are many more flags available for tuning cluster balancing performance (see [Legacy flags](#legacy-flags)). In cases where the cluster has a significant amount of data, the defaults should be sufficient. Increasing the defaults may be required if a cluster has many thousands of tablets with very little data.

## Flags

### Current flags

The following flags are recommended for use in recent versions (2025.1.1.0-b144 and later):

| Flag | Description |
| :--- | :---------- |
| `remote_bootstrap_rate_limit_bytes_per_sec` | The maximum transmission rate during a remote bootstrap. This is the total limit across all remote bootstrap sessions for which this process is acting as a sender or receiver. The total limit is therefore 2 × `remote_bootstrap_rate_limit_bytes_per_sec` because a YB-TServer or YB-Master can act as both a sender and receiver at the same time. |
| `enable_load_balancing` | Choose whether to enable cluster balancing. **Note:** Disabling the cluster balancer prevents automatic re-replication after node failure and can thus cause long-term durability issues. We strongly recommend not disabling the cluster balancer. |

### Legacy flags

The following flags are available in older releases for fine-tuning cluster balancing performance. These flags are deprecated in favor of the automatic defaults in versions 2025.1.1.0-b144 and later.

| Flag | Description |
| :--- | :---------- |
| `load_balancer_max_concurrent_tablet_remote_bootstraps` | The maximum number of tablets being remote bootstrapped across the cluster. If set to -1, there is no global limit on the number of concurrent remote bootstraps (per-table or per YB-TServer limits still apply). |
| `load_balancer_max_concurrent_tablet_remote_bootstraps_per_table` | The maximum number of tablets being remote bootstrapped for any table. The maximum number of remote bootstraps across the cluster is still limited by the flag `load_balancer_max_concurrent_tablet_remote_bootstraps`. This flag prevents a single table from using all the available remote bootstrap sessions and starving other tables. |
| `load_balancer_max_inbound_remote_bootstraps_per_tserver` | Maximum number of tablets simultaneously remote bootstrapping on a tserver. |
| `load_balancer_min_inbound_remote_bootstraps_per_tserver` | Minimum number of tablets simultaneously remote bootstrapping on a tserver (if any are required). |
| `load_balancer_max_over_replicated_tablets` | Maximum number of running tablet replicas per table that are allowed to be over the configured replication factor. This controls the amount of space amplification in the cluster when tablet removal is slow. A value less than 0 means no limit. |
| `load_balancer_max_concurrent_adds` | Maximum number of tablet peer replicas to add in any one run of the cluster balancer. |
| `load_balancer_max_concurrent_removals` | Maximum number of over-replicated tablet peer removals to do in any one run of the cluster balancer. A value less than 0 means no limit. |
| `load_balancer_max_concurrent_moves` | Maximum number of tablet leaders on tablet servers (across the cluster) to move in any one run of the cluster balancer. |
| `load_balancer_max_concurrent_moves_per_table` | Maximum number of tablet leaders per table to move in any one run of the cluster balancer. The maximum number of tablet leader moves across the cluster is still limited by the flag `load_balancer_max_concurrent_moves`. This flag is meant to prevent a single table from using all of the leader moves quota and starving other tables. If set to -1, the number of leader moves per table is set to the global number of leader moves (`load_balancer_max_concurrent_moves`). |

## Metrics

The following metrics can be used to monitor cluster balancing progress:

| Metric | Description |
| :----- | :---------- |
| `estimated_data_to_balance_bytes` | The estimated amount of data (in bytes) that needs to be moved for the cluster to be balanced. |
| `total_table_load_difference` | The estimated number of tablet moves required for the cluster to be balanced. |
| `blacklisted_leaders` | The number of leaders on leader blacklisted tservers. |
| `tablets_in_wrong_placement` | The number of tablet peers in invalid or blacklisted tservers. (An invalid tserver is one that is not part of the tablet's current placement policy). |
| `load_balancer_duration` | How long the previous cluster balancer run took, in milliseconds. |

## Learn more

- [YFTT: Cluster Balancing](http://youtube.com/watch?v=AiIM3bb5kS8)
---
title: Cluster balancing
headerTitle: Cluster balancing
linkTitle: Cluster balancing
description: Learn about cluster balancing (load balancing) mechanisms in YugabyteDB.
menu:
  stable:
    identifier: docdb-cluster-balancing
    parent: architecture-docdb-sharding
    weight: 1144
type: docs
---

## Overview

Cluster balancing is the process by which YugabyteDB automatically distributes data and queries across the nodes in a cluster to maintain fault tolerance and maximize performance. This page describes the various scenarios in which YugabyteDB performs cluster balancing and how to monitor its progress.

Cluster balancing is performed by a component of the [YB-Master](../../yb-master/) called the cluster balancer. The cluster balancer continuously monitors the cluster configuration and moves [tablet](../../key-concepts/#tablet) data and [leaders](../../key-concepts/#tablet-leader) to evenly distribute data and query load, thus distributing CPU, disk, and network load across the cluster.

{{< note title="Terminology" >}}

Many of the flags and metrics on this page refer to the cluster balancer as the "load balancer". This document uses the more precise term "cluster balancer" to avoid confusion with network load balancers or client-side load balancers.

{{< /note >}}

### Disable cluster balancing

The cluster balancer can be disabled by setting the flag [enable_load_balancing](#enable-load-balancing) to `false` on YB-Master.

{{< warning title="Warning" >}}

It is strongly recommended to not disable the cluster balancer, as disabling the cluster balancer prevents automatic re-replication after node failure and can thus cause long-term durability issues.

{{< /warning >}}

## Background

### Placement policy

A placement policy is a specification of how data and queries should be distributed across the cluster. For example, a typical [replication factor](../../key-concepts/#replication-factor-rf) of 3 (RF-3) placement might specify that there should be one replica of each [tablet](../../key-concepts/#tablet) in three separate regions (for example, `us-west-1a`, `us-east-2a`, and `us-central-1a`), with [leader](../../key-concepts/#tablet-leader) preference towards `us-west-1a`.

The default placement policy is automatically inferred when a cluster is created, but it can be modified at the cluster-level with [yb-admin](../../../admin/yb-admin/), or overridden on a per-table basis with [tablespaces](../../../explore/going-beyond-sql/tablespaces/) (for YSQL clusters).

### Remote bootstrap

The cluster balancer creates new [tablet](../../key-concepts/#tablet) replicas by a process called remote bootstrap. This involves copying data from a [YB-TServer](../../yb-tserver/) hosting an up-to-date [tablet peer](../../docdb-replication/replication/#tablet-peers) (typically, the geographically closest peer) to the destination [YB-TServer](../../yb-tserver/).

### Blacklisting

A [YB-TServer](../../yb-tserver/) can be blacklisted or "leader blacklisted" to prevent it from hosting data or [leaders](../../key-concepts/#tablet-leader), respectively. Blacklists are used to facilitate graceful shutdowns (for example, when performing staged restarts or OS patching on a node).

## Cluster balancing scenarios

Cluster balancing automatically occurs when a cluster is scaled in or out, when there are node outages (for example, a failed node, or a network partition), and after creating or deleting tables and tablets.

### Cluster balancing when scaling

#### Scaling out

When scaling out (adding nodes), the cluster balancer will gracefully move [tablet](../../key-concepts/#tablet) data and [leaders](../../key-concepts/#tablet-leader) to the new nodes, without any application impact. [Tablet leaders](../../key-concepts/#tablet-leader) handle queries, so moving leaders is equivalent to moving where the queries execute.

#### Scaling in

When scaling in, [YugabyteDB Anywhere](../../../yugabyte-platform/) marks the nodes being removed as "blacklisted". This signals to the cluster balancer that it should:

- Gracefully transfer [leader](../../key-concepts/#tablet-leader)ship off of the node being removed.
- Re-replicate the data that was on the node to other nodes in the same region.

After all data and queries have moved off of the node, the node can be removed safely.

### Cluster balancing after node outages

On an unplanned node outage, cluster balancing occurs twice: when the node goes down, and if or when it recovers.

#### When the node goes down

- [Tablet leader](../../key-concepts/#tablet-leader)ship is automatically transferred to healthy nodes in 3 seconds.
- If the node is down for a prolonged time (15 minutes, by default) the cluster balancer starts creating new replicas of the data on the failed node on other nodes in the same region (if any exist).
- Queries will succeed as soon as [tablet leaders](../../key-concepts/#tablet-leader) move off of the failed node. The data balancing phase does not impact the availability of the database.

#### When the failed node recovers

If or when the failed node recovers, the cluster balancer rebalances [tablet leaders](../../key-concepts/#tablet-leader) onto the recovered node, as well as [tablet](../../key-concepts/#tablet) data (if data had moved off of the node).

### Table and tablet creation and deletion

Operations that create or delete tablets may also trigger cluster balancing. Some examples of operations that may trigger cluster balancing are:

- Creating a table
- Dropping a table
- [Tablet splitting](../tablet-splitting/)

## Monitoring cluster balancer progress

{{< note title="Version availability" >}}

Some of the views below are only available in YugabyteDB {{<release "2024.2.6">}} and later, {{<release "2025.1.1">}} and later.

{{< /note >}}

While leader rebalancing is fast, data rebalancing can take anywhere from seconds to hours. The major factors influencing how long data rebalancing takes are:

- The size of data on the failed node.
- The configured network and disk throughput.
- Cluster balancer configuration flags (described in the [Flags section](#flags)).

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="#yugabyted" class="nav-link active" id="yugabyted-tab" data-bs-toggle="tab" role="tab" aria-controls="yugabyted" aria-selected="true">
      <img src="/icons/database.svg" alt="Database Icon">
      yugabyted
    </a>
  </li>
  <li>
    <a href="#yba" class="nav-link" id="yba-tab" data-bs-toggle="tab" role="tab" aria-controls="yba" aria-selected="false">
      <img src="/icons/server.svg" alt="Server Icon">
      YugabyteDB Anywhere
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="yugabyted" class="tab-pane fade show active" role="tabpanel" aria-labelledby="yugabyted-tab">

With yugabyted, you can access the YB-Master UI at <http://localhost:7000> to access two views to monitor the cluster balancer progress.

#### Tablet Servers view

The "Tablet Servers" view contains information about how many [peers](../../docdb-replication/replication/#tablet-peers) and [leaders](../../key-concepts/#tablet-leader) are hosted on each tablet server, as well as whether each tablet server is currently data or leader blacklisted.

To access the view, click **Tablet Servers** on your YB-Master UI.

#### Cluster Balancer view

The Cluster Balancer view displays information about:

- Ongoing cluster balancing tasks (for example, [tablet leader](../../key-concepts/#tablet-leader)ship movement, [tablets](../../key-concepts/#tablet) being added or removed).
- Any warnings during cluster balancing (for example, a placement policy being unsatisfiable because there are no TServers in the requested zone)
- Ongoing remote bootstraps from one tablet server to another, including the speed of data transfer.

To access the view, click **Utilities > Load Balancer** on your YB-Master UI.

  </div>
  <div id="yba" class="tab-pane fade" role="tabpanel" aria-labelledby="yba-tab">

The [YugabyteDB Anywhere](../../../yugabyte-platform/) [Metrics](../../../yugabyte-platform/alerts-monitoring/anywhere-metrics/) page has graphs to monitoring cluster balancing progress.

You access metrics by navigating to **Universes > Universe-Name > Metrics**.

#### Data Disk Usage graph

The "Data Disk Usage" graph displays the amount of disk space used by the selected tablet server. The disk usage should be similar on all tablet servers after data is balanced.

To view the graph, select the **Resources** tab on your universe **Metrics** page.

#### Cluster load balancer statistics graph

The "Cluster load balancer statistics" graph displays an estimate of the number of [tablets](../../key-concepts/#tablet) that still have to be moved before the cluster is considered balanced.

To view the graph, select the **Master Server** tab on your universe **Metrics** page as per the following illustration:

![Cluster load balancer statistics](/images/architecture/cluster-balancer-graph.png)

Cluster balancing is complete when this graph reaches 0.

#### Monitoring progress from metrics

The graph above shows the following metrics, which can be used to track the number of [tablet](../../key-concepts/#tablet) moves that are required before the cluster is considered balanced:

- `total_table_load_difference`
- `blacklisted_leaders`
- `tablets_in_wrong_placement`

The metric `estimated_data_to_balance_bytes` tracks how much data (in bytes) must be transferred before a [cluster](../../key-concepts/#cluster) is balanced. This is often the best indicator of how long cluster balancing will take, as many clusters are disk or network limited.

  </div>
</div>

## Configuration and tuning

Starting in {{<release "2025.1.1">}} and later, you only need to set the flag, [remote_bootstrap_rate_limit_bytes_per_sec](#current-flags), which controls how much data each node will send or receive each second. This flag should be set according to your network and disk provisioning limits. You should take into account the requirements of your workload.

For example, if you provisioned your network and disk to 1 GiB/s, and your workload typically uses 500 MiB/s of network and disk, you might want to set `remote_bootstrap_rate_limit_bytes_per_sec` to 300 MiB/s to leave some headroom (200 MiB/s) for workload spikes. Alternatively, you could set it to 500 MiB/s to minimize the time taken for node recovery and cluster scaling operations (at the cost of some workload impact).

{{< warning title="Upgrades" >}}

If you upgrade YugabyteDB to a version after {{<release "2025.1.1">}}, you must unset the older flags (listed in the [Legacy flags](#legacy-flags) section) for YugabyteDB to use the faster cluster balancing defaults.

{{< /warning >}}

For versions prior to {{<release "2025.1.1">}}, there are many more flags available for tuning cluster balancing performance (see [Legacy flags](#legacy-flags)). In cases where the cluster has a significant amount of data, the defaults should be sufficient. Increasing the defaults may be required if a cluster has many thousands of tablets with very little data.

## Flags

### Current flags

The following flags are recommended for use in {{<release "2025.1.1">}} and later.

| Flag | Description |
| :--- | :---------- |
| `remote_bootstrap_rate_limit_bytes_per_sec` | The maximum transmission rate during a remote bootstrap. This is the total limit across all remote bootstrap sessions for which this process is acting as a sender or receiver. The total limit is therefore 2 × `remote_bootstrap_rate_limit_bytes_per_sec` because a [YB-TServer](../../yb-tserver/) or [YB-Master](../../yb-master/) can act as both a sender and receiver at the same time. |
| `enable_load_balancing` | Choose whether to enable cluster balancing. **Note:** It is strongly recommended to not disable the cluster balancer, as disabling the cluster balancer prevents automatic re-replication after node failure and can thus cause long-term durability issues. |

### Legacy flags

The following flags are available in releases prior to {{<release "2025.1.1">}} for fine-tuning cluster balancing performance. These flags are deprecated in favor of the automatic defaults in versions {{<release "2025.1.1">}} and later.

| Flag | Description |
| :--- | :---------- |
| `load_balancer_max_concurrent_tablet_remote_bootstraps` | The maximum number of [tablets](../../key-concepts/#tablet) being remote bootstrapped across the cluster. If set to -1, there is no global limit on the number of concurrent remote bootstraps (per-table or per [YB-TServer](../../yb-tserver/) limits still apply). |
| `load_balancer_max_concurrent_tablet_remote_bootstraps_per_table` | The maximum number of [tablets](../../key-concepts/#tablet) being remote bootstrapped for any table. The maximum number of remote bootstraps _across the cluster_ is still limited by the flag `load_balancer_max_concurrent_tablet_remote_bootstraps`. This flag prevents a single table from using all the available remote bootstrap sessions and starving other tables. |
| `load_balancer_max_inbound_remote_bootstraps_per_tserver` | Maximum number of [tablets](../../key-concepts/#tablet) simultaneously remote bootstrapping on a [YB-TServer](../../yb-tserver/). |
| `load_balancer_min_inbound_remote_bootstraps_per_tserver` | Minimum number of [tablets](../../key-concepts/#tablet) simultaneously remote bootstrapping on a [YB-TServer](../../yb-tserver/) (if any are required). |
| `load_balancer_max_over_replicated_tablets` | Maximum number of running [tablet](../../key-concepts/#tablet) replicas per table that are allowed to be over the configured [replication factor](../../key-concepts/#replication-factor-rf). This controls the amount of space amplification in the cluster when tablet removal is slow. A value less than 0 means no limit. |
| `load_balancer_max_concurrent_adds` | Maximum number of [tablet peer](../../docdb-replication/replication/#tablet-peers) replicas to add in any one run of the cluster balancer. |
| `load_balancer_max_concurrent_removals` | Maximum number of over-replicated [tablet peer](../../docdb-replication/replication/#tablet-peers) removals to do in any one run of the cluster balancer. A value less than 0 means no limit. |
| `load_balancer_max_concurrent_moves` | Maximum number of [tablet leaders](../../key-concepts/#tablet-leader) on tablet servers (across the cluster) to move in any one run of the cluster balancer. |
| `load_balancer_max_concurrent_moves_per_table` | Maximum number of [tablet leaders](../../key-concepts/#tablet-leader) per table to move in any one run of the cluster balancer. The maximum number of tablet leader moves _across the cluster_ is still limited by the flag `load_balancer_max_concurrent_moves`. This flag is meant to prevent a single table from using all of the leader moves quota and starving other tables. If set to -1, the number of leader moves per table is set to the global number of leader moves (`load_balancer_max_concurrent_moves`). |

## Metrics

The following metrics can be used to monitor cluster balancing progress:

| Metric | Description |
| :----- | :---------- |
| `estimated_data_to_balance_bytes` | The estimated amount of data (in bytes) that needs to be moved for the cluster to be balanced. |
| `total_table_load_difference` | The estimated number of [tablet](../../key-concepts/#tablet) moves required for the cluster to be balanced. |
| `blacklisted_leaders` | The number of [leaders](../../key-concepts/#tablet-leader) on leader blacklisted TServers. |
| `tablets_in_wrong_placement` | The number of [tablet peers](../../docdb-replication/replication/#tablet-peers) in invalid or blacklisted TServers. (An invalid TServer is one that is not part of the tablet's current placement policy). |
| `load_balancer_duration` | How long the previous cluster balancer run took, in milliseconds. |

## Learn more

- [YFTT: Cluster Balancing](http://youtube.com/watch?v=AiIM3bb5kS8)
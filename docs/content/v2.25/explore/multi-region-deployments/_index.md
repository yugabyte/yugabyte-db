---
title: Multi-Region Deployments
headerTitle: Multi-Region Deployments
linkTitle: Multi-region deployments
description: Explore different Multi-Region deployment topologies in YugabyteDB.
headcontent: Multi-Region Deployments in YugabyteDB.
menu:
  v2.25:
    identifier: explore-multi-region-deployments
    parent: explore
    weight: 270
type: indexpage
---
YugabyteDB supports a rich set of multi-region deployment topologies. This section explains some of these deployments. The predominant deployments include:

* **Default synchronous** replication across regions
* **Geo-partitioning** to pin data to different geographic locations based on policy
* **xCluster** for unidirectional and bidirectional asynchronous replication
* **Read replicas** to serve reads to different region using asynchronous replication

The following table summarizes these different multi-region deployments in YugabyteDB along with some of their key characteristics.

|     | [Default](synchronous-replication-ysql/) | [Geo-partitioning](row-level-geo-partitioning/) | [xCluster](../going-beyond-sql/asynchronous-replication-ysql/) | [Read replicas](read-replicas-ysql/) |
| :-- | :--------------------------------------- | :---------------------------------------------- | :----------------------------------------- | :------------ |
| **Replication** | Synchronous | Synchronous  | Asynchronous <br/> *(unidirectional and bidirectional)* | Asynchronous <br/> *(unidirectional only)* |
| **Data residency** | All data replicated across regions | Data partitioned across regions. <br/>Partitions replicated inside region. | All data replicated inside region. <br/>Configure per-table cross-region replication. | All data replicated in primary region. <br/>Cluster-wide asynchronous replication to read replicas. |
| **Consistency** | Transactional | Transactional | Transactional | Timeline consistency |
| **Write latency** | High latency | Low latency | Low latency | N/A |
| **Read latency** | High latency | Low latency <br/> *(when queried from nearby geography)* | Low latency | Low latency |
| **Schema changes** | Transparently managed | Transparently managed | Transparently managed *([Limitations](../../architecture/docdb-replication/async-replication/#transactional-automatic-mode))* | Transparently managed |
| **RPO** | No data loss | No data loss <br/> *(partial unavailability possible)* | Some data loss | No data loss |

The deployment types are explained in the following sections.

{{<index/block>}}

  {{<index/item
    title="Synchronous multi region"
    body="Distribute data synchronously across regions."
    href="synchronous-replication-ysql/"
    icon="fa-thin fa-circle-nodes">}}

  {{<index/item
    title="xCluster"
    body="Asynchronous replication across regions."
    href="../going-beyond-sql/asynchronous-replication-ysql/"
    icon="fa-thin fa-clone">}}

  {{<index/item
    title="Row-level geo-partitioning"
    body="Pin data to regions for compliance and lower latencies."
    href="row-level-geo-partitioning/"
    icon="fa-thin fa-earth-europe">}}

  {{<index/item
    title="Read replicas"
    body="Improve read latencies using read-only replicas."
    href="read-replicas-ysql/"
    icon="fa-thin fa-book-open">}}

{{</index/block>}}

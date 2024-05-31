---
title: Multi-Region Deployments
headerTitle: Multi-Region Deployments
linkTitle: Multi-region deployments
description: Multi-Region Deployments in YugabyteDB.
headcontent: Multi-Region Deployments in YugabyteDB.
image: /images/section_icons/explore/planet_scale.png
menu:
  v2.16:
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

|     | [Default](synchronous-replication-ysql/) | [Geo-partitioning](row-level-geo-partitioning/) | [xCluster](asynchronous-replication-ysql/) | [Read replicas](read-replicas/) |
| :-- | :--------------------------------------- | :---------------------------------------------- | :----------------------------------------- | :------------ |
| **Replication** | Synchronous | Synchronous  | Asynchronous <br/> *(unidirectional and bidirectional)* | Asynchronous <br/> *(unidirectional only)* |
| **Data residency** | All data replicated across regions | Data partitioned across regions. <br/>Partitions replicated inside region. | All data replicated inside region. <br/>Configure per-table cross-region replication. | All data replicated in primary region. <br/>Cluster-wide asynchronous replication to read replicas. |
| **Consistency** | Transactional | Transactional | Timeline consistency | Timeline consistency |
| **Write latency** | High latency | Low latency | Low latency | N/A |
| **Read latency** | High latency | Low latency <br/> *(when queried from nearby geography)* | Low latency | Low latency |
| **Schema changes** | Transparently managed | Transparently managed | Manual propagation | Transparently managed |
| **RPO** | No data loss | No data loss <br/> *(partial unavailability possible)* | Some data loss | No data loss |

The deployment types are explained in the following sections.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="synchronous-replication-ysql/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/rbac-model.png" aria-hidden="true" />
        <div class="title">Synchronous Replication</div>
      </div>
      <div class="body">
          Achieve synchronous replication across three or more regions in YugabyteDB.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="asynchronous-replication-ysql/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/rbac-model.png" aria-hidden="true" />
        <div class="title">xCluster</div>
      </div>
      <div class="body">
          Achieve asynchronous replication across two or more regions in YugabyteDB.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="row-level-geo-partitioning/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/rbac-model.png" aria-hidden="true" />
        <div class="title">Row-Level Geo-Partitioning</div>
      </div>
      <div class="body">
          Use row-level geo-partitioning in YugabyteDB.
      </div>
    </a>
  </div>

   <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="read-replicas/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/rbac-model.png" aria-hidden="true" />
        <div class="title">Read Replicas</div>
      </div>
      <div class="body">
          Use read replicas to lower latency and improve performance.
      </div>
    </a>
  </div>

 <!-- <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="follower-reads-ysql/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/rbac-model.png" aria-hidden="true" />
        <div class="title">Follower Reads</div>
      </div>
      <div class="body">
          Use Follower Reads to reduce read latencies in YugabyteDB.
      </div>
    </a>
  </div>
</div> -->

</div>

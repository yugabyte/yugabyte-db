---
title: Multi-Region Deployments
headerTitle: Multi-Region Deployments
linkTitle: Multi-Region Deployments
description: Multi-Region Deployments in YugabyteDB.
headcontent: Multi-Region Deployments in YugabyteDB.
image: /images/section_icons/secure/authorization.png
aliases:
  - /secure/authorization/
menu:
  latest:
    identifier: explore-multi-region-deployments
    parent: explore
    weight: 700
---

YugabyteDB supports a rich set of multi-region deployment topologies. This section explains some of these deployments. The predominant deployments include:

* **Default synchronous** replication across regions
* **Geo-partitioning** to keep data pinned to different geographic locations based on policy
* **xCluster asynchronous replication** for unidirectional and bidirectional replication
* **Read replicas** which internally use asynchronous replication and can only serve reads

The table below summarizes these different multi-region deployments in YugabyteDB along with some of their key characteristics.

<table>
  <tr>
   <td>
   </td>
   <td><strong>Default</strong>
   </td>
   <td><strong>xCluster</strong>
   </td>
   <td><strong>Geo-partitioning</strong>
   </td>
  </tr>
  <tr>
   <td><strong>Replication</strong>
   </td>
   <td>Synchronous
   </td>
   <td>Asynchronous <br/>(unidirectional and bidirectional)
   </td>
   <td>Synchronous
   </td>
  </tr>
  <tr>
   <td><strong>Data residency</strong>
   </td>
   <td>All data replicated across regions
   </td>
   <td>All data replicated inside region. <br/>Some data replicated across regions.
   </td>
   <td>Data partitioned across regions. <br/>Partitions replicated inside region.
   </td>
  </tr>
  <tr>
   <td><strong>Consistency</strong>
   </td>
   <td>Transactional
   </td>
   <td>Eventual consistency
   </td>
   <td>Transactional
   </td>
  </tr>
  <tr>
   <td><strong>Query latency</strong>
   </td>
   <td>High latency
   </td>
   <td>Low latency
   </td>
   <td>Low latency (when queried from nearby geography)
   </td>
  </tr>
  <tr>
   <td><strong>Schema changes</strong>
   </td>
   <td>Transparently managed
   </td>
   <td>Manual propagation
   </td>
   <td>Transparently managed
   </td>
  </tr>
  <tr>
   <td><strong>RPO (region outage)</strong>
   </td>
   <td>No data loss
   </td>
   <td>Some data loss
   </td>
   <td>No data loss (partial unavailability possible)
   </td>
  </tr>
</table>

The different deployments are explained in the sections below.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="row-level-geo-partitioning/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/rbac-model.png" aria-hidden="true" />
        <div class="title">Row-Level Geo-Partitioning</div>
      </div>
      <div class="body">
          Using row-level geo-partitioning in YugabyteDB.
      </div>
    </a>
  </div>

</div>

---
title: Horizontal scalability
headerTitle: Horizontal scalability
linkTitle: Horizontal scalability
description: Horizontal scalability in YugabyteDB.
headcontent: Dynamically add and remove nodes in a running cluster
image: /images/section_icons/explore/linear_scalability.png
menu:
  v2.16:
    identifier: explore-scalability
    parent: explore
    weight: 220
type: indexpage
---

A YugabyteDB cluster can be scaled horizontally (to increase the aggregate vCPUs, memory, and disk in the database cluster) by dynamically adding nodes to a running cluster, or by increasing the number of pods in the `yb-tserver` StatefulSet in the case of Kubernetes deployments.

A YugabyteDB cluster is scaled out so that it can handle:

* More transactions per second
* Higher number of concurrent client connections
* Larger datasets

A YugabyteDB cluster can also be *scaled in* dynamically by draining all the data from existing cluster nodes (or pods), and subsequently removing them from the cluster.

### How scalability works

Every table in YugabyteDB is transparently sharded using the primary key of the table, and each of these shards are called *tablets*. Each tablet consists of a set of rows in a table. In YugabyteDB, tables are automatically split into tablets. This is done when creating the table if possible. Tablets can also be split dynamically.

The following table summarizes the support for scalability and sharding across YSQL and YCQL APIs.

| Property | YSQL | YCQL | Comments |
| :------- | :--- | :--- | :------- |
| [Scale transactions per sec](scaling-transactions/) | Yes | Yes | Scale out the cluster to handle a higher number of concurrent transactions per second. |
| [Data distribution support](sharding-data/) | Hash sharding<br/>Range sharding | Hash sharding | Sharding is used to distribute data across nodes of clusters.<br/>Tables can be pre-split at creation time, and dynamically split at runtime. |

<div class="row">

   <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="scaling-transactions/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/linear_scalability.png" aria-hidden="true" />
        <div class="title">Scaling transactions per second</div>
      </div>
      <div class="body">
        Scale out the cluster to handle a higher number of concurrent transactions per sec.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="sharding-data/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/auto_sharding.png" aria-hidden="true" />
        <div class="title">Sharding (distributing) data across nodes</div>
      </div>
      <div class="body">
        Automatic data distribution across nodes of the cluster using transparent sharding of tables.
      </div>
    </a>
  </div>

</div>

---
title: Horizontal scalability
headerTitle: Horizontal scalability
linkTitle: Horizontal scalability
description: Horizontal scalability in YugabyteDB.
headcontent: Dynamically add and remove nodes in a running universe
image: /images/section_icons/explore/linear_scalability.png
menu:
  stable:
    identifier: explore-scalability
    parent: explore
    weight: 220
type: indexpage
---

A YugabyteDB universe can be scaled horizontally to increase the aggregate vCPUs, memory, and disk in the database by dynamically adding nodes to a running universe or by increasing the number of pods in the `yb-tserver` StatefulSet in the case of Kubernetes deployments.

A YugabyteDB universe is scaled out so that it can handle the following:

* More transactions per second.
* Greater number of concurrent client connections.
* Larger datasets.

A YugabyteDB universe can also be scaled in dynamically by draining all the data from existing universe nodes (or Kubernetes pods), and subsequently removing them from the universe.

Every table in YugabyteDB is transparently sharded using its primary key. The shards are called tablets. Each tablet consists of a set of rows in a table. In YugabyteDB, tables are automatically split into tablets during the table creation if possible. Tablets can also be split dynamically.

The following table summarizes YugabyteDB support for scalability and sharding across YSQL and YCQL APIs:

| Property | YSQL | YCQL | Comments |
| :------- | :--- | :--- | :------- |
| [Scale transactions per sec](scaling-transactions/) | Yes | Yes | Scale out a universe to handle a greater number of concurrent transactions per second. |
| [Data distribution support](sharding-data/) | Hash sharding,<br/>Range sharding | Hash sharding | Sharding is used to distribute data across a universe's nodes.<br/>Tables can be pre-split at creation time, and dynamically split at runtime. |

<div class="row">
   <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="scaling-transactions-cloud/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/linear_scalability.png" aria-hidden="true" />
        <div class="title">Scaling transactions per second</div>
      </div>
      <div class="body">
        Scaling out a universe to handle a greater number of concurrent transactions per second.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="sharding-data/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/auto_sharding.png" aria-hidden="true" />
        <div class="title">Data distribution across nodes</div>
      </div>
      <div class="body">
        Automatic data distribution across the universe's nodes using transparent sharding of tables.
      </div>
    </a>
  </div>


</div>

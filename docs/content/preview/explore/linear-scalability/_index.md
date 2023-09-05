---
title: Horizontal scalability
headerTitle: Horizontal scalability
linkTitle: Horizontal scalability
description: Horizontal scalability in YugabyteDB.
headcontent: Handle larger workloads by adding nodes to your cluster
aliases:
  - /explore/linear-scalability/
  - /preview/explore/linear-scalability/
  - /preview/explore/postgresql/linear-scalability/
  - /preview/explore/linear-scalability-macos/
  - /preview/explore/linear-scalability/linux/
  - /preview/explore/linear-scalability/docker/
  - /preview/explore/linear-scalability/kubernetes/
  - /preview/explore/auto-sharding/macos/
  - /preview/explore/auto-sharding/linux/
  - /preview/explore/auto-sharding/docker/
  - /preview/explore/auto-sharding/kubernetes/
image: /images/section_icons/explore/linear_scalability.png
menu:
  preview:
    identifier: explore-scalability
    parent: explore
    weight: 220
type: indexpage
---


A YugabyteDB universe can be scaled-out to handle the following:

* More transactions per second
* A higher number of concurrent client connections
* Larger datasets or workloads

YugabyteDB can be scaled either horizontally or vertically depending on your needs as described in the following sections.

## Horizontal scaling (scaling-out)

Horizontal scaling involves adding more nodes to a distributed database to handle increased load, traffic, and data. In YugabyteDB, data is shared and split into tablets, and these multiple tablets are located on each node. When more nodes are added, some tablets are automatically moved to the new nodes. Tablets will also split dynamically as needed to use the newly added resource which leads to each node managing fewer tablets. The entire cluster can therefore handle more transactions and queries in parallel, thus increasing its capacity to handle larger workloads.

Horizontal scaling is the most common scaling model advised for YugabyteDB as it has several advantages.

* **Improved performance**: More nodes can process requests in parallel, reducing response times.
* **Cost-effective**: Can utilize commodity hardware, which is generally less expensive than high-end servers.
* **Resource expansion**: You can add new nodes as needed to accommodate growth.

You can consider scaling-out your cluster to temporarily handle high traffic (For example, Black Friday shopping, major news outbreak) and reduce the size of the cluster (*scaling-in*) after the event by draining all the data from some of the nodes (or Kubernetes pods) and subsequently removing them from the universe.

## Vertical scaling (scaling-up)

Vertical scaling involves upgrading the existing hardware or resources of each of the nodes in your cluster. Instead of adding more machines, you enhance the capabilities of a single machine by increasing its CPU, memory, storage, and so on. Vertical scaling is often limited by the capacity of a single server and can get **_expensive_** as you move to more powerful hardware. Although you retain the same number of nodes which could simplify your operations, eventually hardware resources will reach their limits, and further scaling might not be feasible.

In some cases, depending on your application needs and budget constraints, a combination of both horizontal and vertical scaling (also known as elastic scaling) may be used to achieve the desired performance and scalability goals.

## Scaling in YSQL vs YCQL

The following table summarizes YugabyteDB support for scalability and sharding across [YSQL](../../api/ysql/) and [YCQL](../../api/ycql/) APIs:

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

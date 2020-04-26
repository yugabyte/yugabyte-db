---
title: DocDB sharding layer
headerTitle: DocDB sharding layer
linkTitle: DocDB sharding layer
description: Learn about the YugabyteDB distributed document store that is responsible for sharding, replication, transactions, and persistence.
image: /images/section_icons/architecture/concepts.png
aliases:
  - /latest/architecture/docdb/sharding
headcontent: How automatic sharding of data works in YugabyteDB.
menu:
  latest:
    identifier: architecture-docdb-sharding
    parent: architecture
    weight: 1130
---

This section describes how sharding works in DocDB. A distributed SQL database needs to automatically partition the data in a table and distribute it across nodes. This is known as data sharding and it can be achieved through different strategies, each with its own tradeoffs.

Data sharding helps in scalability and geo-distribution by horizontally partitioning data. A SQL table is decomposed into multiple sets of rows according to a specific sharding strategy. Each of these sets of rows is called a shard. These shards are distributed across multiple server nodes (containers, VMs, bare-metal) in a shared-nothing architecture. This ensures that the shards do not get bottlenecked by the compute, storage and networking resources available at a single node. High availability is achieved by replicating each shard across multiple nodes. However, the application interacts with a SQL table as one logical unit and remains agnostic to the physical placement of the shards.

DocDB supports range and hash sharding natively.

{{< note title="Note" >}}
Read more about the [tradeoffs in the various sharding strategies considered](https://blog.yugabyte.com/four-data-sharding-strategies-we-analyzed-in-building-a-distributed-sql-database/).
{{</note >}}


<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="sharding/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/concepts/sharding.png" aria-hidden="true" />
        <div class="title">Sharding strategies</div>
      </div>
      <div class="body">
        The supported sharding strategies in DocDB, hash and range sharding.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="replication/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/concepts/replication.png" aria-hidden="true" />
        <div class="title">Tablet splitting</div>
      </div>
      <div class="body">
        How tablet splitting works in DocDB. This includes pre-splitting tablets, manual splitting and dynamic splitting.
      </div>
    </a>
  </div>

</div>
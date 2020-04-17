---
title: DocDB store
headerTitle: DocDB store
linkTitle: DocDB store
description: Learn about the YugabyteDB distributed document store that is responsible for sharding, replication, transactions, and persistence.
image: /images/section_icons/architecture/concepts.png
aliases:
  - /architecture/concepts/docdb/
  - /latest/architecture/concepts/docdb/
headcontent: YugabyteDB distributed document store responsible for sharding, replication, transactions, and persistence.
menu:
  latest:
    identifier: docdb
    parent: architecture
    weight: 1140
---

{{< note title="Note" >}}

YugabyteDB's sharding and replication architecture is inspired by <a href="https://research.google.com/archive/spanner-osdi2012.pdf">Google Spanner</a>.

{{</note >}}

<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="sharding/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/concepts/sharding.png" aria-hidden="true" />
        <div class="title">Sharding</div>
      </div>
      <div class="body">
        Sharding the data in every table into tablets.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="replication/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/concepts/replication.png" aria-hidden="true" />
        <div class="title">Replication</div>
      </div>
      <div class="body">
        Replicating the data in every table with Raft consensus.
      </div>
    </a>
  </div>

 <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="persistence/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/json_documents.png" aria-hidden="true" />
        <div class="title">Persistence</div>
      </div>
      <div class="body">
        Per-node document-based storage engine built by extending RocksDB.
      </div>
    </a>
  </div>

 <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="performance/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/high_performance.png" aria-hidden="true" />
        <div class="title">Performance</div>
      </div>
      <div class="body">
        Achieving high performance in DocDB.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="colocated-tables/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/linear_scalability.png" aria-hidden="true" />
        <div class="title">Colocated tables</div>
      </div>
      <div class="body">
        Scaling number of relations and databases by colocating data.
      </div>
    </a>
  </div>

</div>

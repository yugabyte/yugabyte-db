---
title: Transactions Layer
headerTitle: Transactions Layer
linkTitle: Transactions Layer
description: Transactions Layer
image: /images/section_icons/architecture/distributed_acid.png
headcontent: 
aliases:
  - /architecture/transactions/
menu:
  latest:
    identifier: architecture-acid-transactions
    parent: architecture
    weight: 1120
---

{{< note title="Note" >}}

YugabyteDB's distributed ACID transaction architecture is inspired by <a href="https://research.google.com/archive/spanner-osdi2012.pdf">Google Spanner</a>.

{{</note >}}

<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="isolation-levels/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/distributed_acid.png" aria-hidden="true" />
        <div class="title">Isolation levels</div>
      </div>
      <div class="body">
        Understanding supported transaction isolation levels (the "I" in ACID).
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="single-row-transactions/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/distributed_acid.png" aria-hidden="true" />
        <div class="title">Single row transactions</div>
      </div>
      <div class="body">
        How single row transactions work in YugabyteDB
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="distributed-txns/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/distributed_acid.png" aria-hidden="true" />
        <div class="title">Distributed transactions</div>
      </div>
      <div class="body">
        How distributed (aka multi-shard) transactions work in YugabyteDB
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="transactional-io-path/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/distributed_acid.png" aria-hidden="true" />
        <div class="title">Transactional IO path</div>
      </div>
      <div class="body">
        How reads and writes occur during transactions
      </div>
    </a>
  </div>
</div>

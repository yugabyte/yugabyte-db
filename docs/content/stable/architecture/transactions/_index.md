---
title: DocDB transactions layer
headerTitle: DocDB transactions layer
linkTitle: DocDB transactions layer
description: DocDB transactions layer
image: /images/section_icons/architecture/distributed_acid.png
headcontent: DocDB is YugabyteDB's distributed document store responsible for transactions, sharding, replication, and persistence.
menu:
  stable:
    identifier: architecture-acid-transactions
    parent: architecture
    weight: 1120
type: indexpage
---

YugabyteDB's distributed transaction architecture is based on principles of atomicity, consistency, isolation, and durability (ACID), and is inspired by <a href="https://research.google.com/archive/spanner-osdi2012.pdf">Google Spanner</a>.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="transactions-overview/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/distributed_acid.png" aria-hidden="true" />
        <div class="title">Overview</div>
      </div>
      <div class="body">
        Transactions support in YugabyteDB.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="isolation-levels/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/distributed_acid.png" aria-hidden="true" />
        <div class="title">Transaction isolation levels</div>
      </div>
      <div class="body">
        Serializable, Read Committed, and Snapshot isolation levels in YugabyteDB.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="concurrency-control/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/distributed_acid.png" aria-hidden="true" />
        <div class="title">Concurrency control</div>
      </div>
      <div class="body">
        Learn how YugabyteDB handles conflicts between concurrent transactions.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="transaction-priorities/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/distributed_acid.png" aria-hidden="true" />
        <div class="title">Transaction priorities</div>
      </div>
      <div class="body">
        Learn how YugabyteDB decides which transactions should be aborted in case of conflict.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="read-committed/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/distributed_acid.png" aria-hidden="true" />
        <div class="title">Read committed</div>
      </div>
      <div class="body">
        Read committed isolation level in YugabyteDB.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="single-row-transactions/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/distributed_acid.png" aria-hidden="true" />
        <div class="title">Single-row transactions</div>
      </div>
      <div class="body">
        Single-row transactions in YugabyteDB.
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
        Distributed (multi-shard) transactions in YugabyteDB.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="transactional-io-path/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/distributed_acid.png" aria-hidden="true" />
        <div class="title">Transactional I/O path</div>
      </div>
      <div class="body">
        Transactional reads and writes in YugabyteDB.
      </div>
    </a>
  </div>
</div>

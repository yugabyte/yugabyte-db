---
title: Transactions
headerTitle: Transactions
linkTitle: Transactions
description: Transactions in YugabyteDB.
headcontent: Transactions in YugabyteDB.
menu:
  latest:
    identifier: explore-transactions
    parent: explore
    weight: 230
---

YugabyteDB is a transactional database that supports distributed transactions. A transaction is a sequence of operations performed as a single logical unit of work. A transaction has four key properties - **Atomicity**, **Consistency**, **Isolation** and **Durability** - commonly abbreviated as ACID.


The table below summarizes the support for transactions across YSQL and YCQL APIs.

| <span style="font-size:20px;">Property</span> | <span style="font-size:20px;">YSQL</span> | <span style="font-size:20px;">YCQL</span> | <span style="font-size:20px;">Comments</span> |
|--------------------------------------------------|-------------|----------|----------|
| <span style="font-size:16px;">[Distributed transactions](distributed-transactions/)</span> | <span style="font-size:16px;">Yes</span> | <span style="font-size:16px;">Yes</span> | Perform multi-row or multi-table transactions. <br/> Application can connect to any node of the cluster. |
| <span style="font-size:16px;">[Isolation levels](isolation-levels/)</span> | <span style="font-size:16px;">Serializable <br/>Snapshot</span>  | <span style="font-size:16px;">Snapshot</span> | *Repeatable read* isolation level in PostgreSQL maps to <br/>*snapshot* isolation in YSQL |

The various features are explained in the sections below.

<div class="row">

   <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="distributed-transactions/">
      <div class="head">
        <div class="icon"><i class="fas fa-sitemap"></i></div>
        <div class="title">Distributed Transactions</div>
      </div>
      <div class="body">
        Understand how distributed transactions work in YugabyteDB.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="isolation-levels/">
      <div class="head">
        <div class="icon"><i class="fas fa-stream"></i></div>
        <div class="title">Isolation Levels</div>
      </div>
      <div class="body">
        Details about isolation levels in YugabyteDB.
      </div>
    </a>
  </div>

</div>




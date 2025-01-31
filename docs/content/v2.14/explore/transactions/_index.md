---
title: Transactions
headerTitle: Transactions
linkTitle: Transactions
description: Transactions in YugabyteDB.
headcontent: Transactions in YugabyteDB.
menu:
  v2.14:
    identifier: explore-transactions
    parent: explore
    weight: 240
type: indexpage
---
YugabyteDB is a transactional database that supports distributed transactions. A transaction is a sequence of operations performed as a single logical unit of work. A transaction has four key properties - **Atomicity**, **Consistency**, **Isolation** and **Durability** - commonly abbreviated as ACID.


The table below summarizes the support for transactions across YSQL and YCQL APIs.

| <span style="font-size:20px;">Property</span> | <span style="font-size:20px;">YSQL</span> | <span style="font-size:20px;">YCQL</span> | <span style="font-size:20px;">Comments</span> |
|--------------------------------------------------|-------------|----------|----------|
| <span style="font-size:16px;">[Distributed transactions](distributed-transactions-ysql/)</span> | <span style="font-size:16px;">Yes</span> | <span style="font-size:16px;">Yes</span> | Perform multi-row or multi-table transactions. <br/> Application can connect to any node of the cluster. |
| <span style="font-size:16px;">[Isolation levels](isolation-levels/)</span> | <span style="font-size:16px;">Serializable <br/>Snapshot</span>  | <span style="font-size:16px;">Snapshot</span> | *Repeatable read* isolation level in PostgreSQL maps to <br/>*snapshot* isolation in YSQL |
| Set `AUTOCOMMIT = false` | <span style="font-size:16px;">Yes</span> | <span style="font-size:16px;">No</span> | The transaction must be expressed as one statement in YCQL. |

The various features are explained in the sections below.

<div class="row">

   <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="distributed-transactions-ysql/">
      <div class="head">
        <div class="icon"><i class="fa-solid fa-sitemap"></i></div>
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
        <div class="icon"><i class="fa-solid fa-bars-staggered"></i></div>
        <div class="title">Isolation Levels</div>
      </div>
      <div class="body">
        Details about isolation levels in YugabyteDB.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="explicit-locking/">
      <div class="head">
        <div class="icon"><i class="fa-solid fa-lock"></i></div>
        <div class="title">Explicit Locking</div>
      </div>
      <div class="body">
        Explicit row-level locking in YSQL.
      </div>
    </a>
  </div>
<!-- ADD THIS ONCE READY:
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="ddl-operations/">
      <div class="head">
        <div class="icon"><i class="fa-solid fa-table"></i></div>
        <div class="title">DDL Operations</div>
      </div>
      <div class="body">
        How YugabyteDB handles DDL operations in transaction blocks.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="non-transactional-tables/">
      <div class="head">
        <div class="icon"><i class="fa-solid fa-strikethrough"></i></div>
        <div class="title">Non-Transactional Tables</div>
      </div>
      <div class="body">
        Disable multi-row transactions on a per-table basis in YCQL.
      </div>
    </a>
  </div>
-->
</div>

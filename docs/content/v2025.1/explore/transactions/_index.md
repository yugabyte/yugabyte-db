---
title: Transactions
headerTitle: Transactions
linkTitle: Transactions
description: Transactions in YugabyteDB.
headcontent: How transactions work in YugabyteDB
menu:
  v2025.1:
    identifier: explore-transactions
    parent: explore
    weight: 240
type: indexpage
---

YugabyteDB is a transactional database that supports distributed transactions. A transaction is a sequence of operations performed as a single logical unit of work. YugabyteDB provides [ACID](../../architecture/key-concepts/#acid) guarantees for all transactions.

The following table summarizes the support for transactions across the YSQL and YCQL APIs.

| Property | YSQL | YCQL | Comments |
| :------- | :--- | :--- | :------- |
| Distributed transactions | Yes | Yes | Perform multi-row or multi-table transactions.<br/>An application can connect to any node of the cluster. |
| Isolation levels | Serializable<br/>Snapshot<br/>Read Committed | Snapshot | Repeatable read isolation level in PostgreSQL maps to snapshot isolation in YSQL. |
| `AUTOCOMMIT = false` setting | Yes | No | The transaction must be expressed as one statement in YCQL. |
| Explicit locking | Yes | No | Ability to perform row- and table-level locking |
| {{<tags/feature/tp idea="1677">}} Transactional DDL | Yes  | No | Each DDL statement is a transaction in YSQL, even if other DDL statements are in a transaction block. |

<!--
| [Non-transactional tables] | No | Yes | Ability to disable multi-row transactions on a per-table basis. <br/>Useful for some features such as automatic data expiry. |
-->

{{<index/block>}}

  {{<index/item
    title="Distributed transactions"
    body="Distributed transactions in YugabyteDB."
    href="distributed-transactions-ysql/"
    icon="fa-thin fa-sitemap">}}

  {{<index/item
    title="Isolation levels"
    body="Serializable, snapshot, and read committed isolation in YugabyteDB."
    href="isolation-levels/"
    icon="fa-thin fa-bars-staggered">}}

  {{<index/item
    title="Explicit locking"
    body="Explicit row-level locking in YSQL."
    href="explicit-locking/"
    icon="fa-thin fa-lock">}}

  {{<index/item
    title="Transactional DDL"
    body="How YugabyteDB handles DDL operations in a transaction block."
    href="transactional-ddl/"
    icon="fa-thin fa-table">}}

{{</index/block>}}

<!-- ADD THIS ONCE READY:

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

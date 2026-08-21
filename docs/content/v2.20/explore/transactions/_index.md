---
title: Transactions
headerTitle: Transactions
linkTitle: Transactions
description: Transactions in YugabyteDB.
headcontent: Transactions in YugabyteDB.
menu:
  v2.20:
    identifier: explore-transactions
    parent: explore
    weight: 240
type: indexpage
---
YugabyteDB is a transactional database that supports distributed transactions. A transaction is a sequence of operations performed as a single logical unit of work. A transaction has four key properties: Atomicity, Consistency, Isolation, Durability (ACID).

The following table summarizes the support for transactions across the YSQL and YCQL APIs.

| Property | YSQL | YCQL | Comments |
| :------- | :--- | :--- | :------- |
| Distributed transactions | Yes | Yes | Perform multi-row or multi-table transactions.<br/>An application can connect to any node of the cluster. |
| Isolation levels | Serializable<br/>Snapshot<br/>Read Committed | Snapshot | Repeatable read isolation level in PostgreSQL maps to snapshot isolation in YSQL. |
| `AUTOCOMMIT = false` setting | Yes | No | The transaction must be expressed as one statement in YCQL. |

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

{{</index/block>}}

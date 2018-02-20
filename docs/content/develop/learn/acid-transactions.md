---
title: ACID Transactions
weight: 565
---

Transaction are a sequence of operations performed as a single logical unit of work. A transaction has four key properties -  **Atomicity**, **Consistency**, **Isolation** and **Durability**. These properties are commonly abbreviated ACID, which is an acronym for for Atomic Consistent Isolated Durability.

- **Atomicity** All the work in the transaction is treated as a single unit - either all of it is performed or none of it is.

- **Consistency** A completed transaction leaves the database in a consistent internal state. This can either be all the operations in the transactions succeeding or none of them succeeding.

- **Isolation** The transaction operates on a consistent view of the data. For example, if two transactions try to update the same table, one will go first and then the other will follow. Currently, YugaByte DB supports *Snapshot Isolation*, and *Serializable* isolation level is in progress. Read more about the different [levels of isolation](/architecture/transactions/isolation-levels/).

- **Durability** The results of the transaction are permanently stored in the system.



<ul class="nav nav-tabs nav-tabs-yb">
  <li class="active">
    <a href="#cassandra">
      <i class="icon-java-bold" aria-hidden="true"></i>
      Cassandra
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="cassandra" class="tab-pane fade in active">
    {{% includeMarkdown "/develop/learn/cassandra/acid-transactions.md" /%}}
  </div>
</div>

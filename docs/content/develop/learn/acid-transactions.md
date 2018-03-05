---
title: ACID Transactions
weight: 565
---

A transaction is a sequence of operations performed as a single logical unit of work. A transaction
has four key properties - **Atomicity**, **Consistency**, **Isolation** and **Durability** -
commonly abbreviated as ACID.

- **Atomicity** All the work in a transaction is treated as a single atomic unit - either all of it
    is performed or none of it is.

- **Consistency** A completed transaction leaves the database in a consistent internal state. This
    can either be all the operations in the transactions succeeding or none of them succeeding.

- **Isolation** This property determines how/when changes made by one transaction become visible to
    the other. For example, a *serializable* isolation level guarantees that two concurrent
    transactions appear as if one executed after the other (i.e. as if they occur in a completely
    isolated fashion). Currently, YugaByte DB supports *Snapshot Isolation*, and *Serializable*
    isolation level is in progress. Read more about the different [levels of
    isolation](/architecture/transactions/isolation-levels/).

- **Durability** The results of the transaction are permanently stored in the system. The
    modifications must persist even in the instance of power loss or system failures.


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

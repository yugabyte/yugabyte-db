---
title: DocDB transactions layer
headerTitle: DocDB transactions layer
linkTitle: Transactions
description: DocDB transactions layer
image: fa-sharp fa-thin fa-money-bill-transfer
headcontent: Understand how distributed transactions work
aliases:
  - /architecture/transactions/
menu:
  stable:
    identifier: architecture-acid-transactions
    parent: architecture
    weight: 900
type: indexpage
---

Transactions and strong consistency are a fundamental requirement for any RDBMS. YugabyteDB's distributed transaction architecture supports fully distributed [ACID](../key-concepts#acid) transactions across rows, multiple tablets, and multiple nodes at any scale and is inspired by [Google Spanner](https://research.google.com/archive/spanner-osdi2012.pdf"). Transactions can span across tables in DocDB.

## Fundamentals

One of the basic challenges in a distributed system is how to manage the time disparity between different machines. This is very critical for distributed transactions and multi version concurrency control. YugabyteDB uses Hybrid Logical clocks to generate a monotonically increasing timestamp.

{{<lead link="transactions-overview/">}}
To learn about more about Hybrid Time and MVCC, see [Transaction fundamentals](transactions-overview/).
{{</lead>}}

## Distributed transactions

Ensuring the [ACID](../key-concepts/#acid) guarantees in a distributed database is a challenge. There are multiple components involved and yet the transaction has to be successfully executed even in the case of component failures.

{{<lead link="distributed-txns/">}}
To understand how failures are handled during transactions, see [Distributed transactions](distributed-txns/).
{{</lead>}}

## Transaction execution

There are multiple components and stages involved in the execution of a distributed transaction from start to commit. A transaction manager co-ordinates the transaction and finally commits or aborts the transaction as needed, the transaction status tablet maintains the status, the provisional database stores the temporary records.

{{<lead link="transactional-io-path/">}}
To understand how a transaction is executed, see [Transactional I/O path](transactional-io-path/).
{{</lead>}}

## Single-row transactions

In cases where keys involved in the transaction are located in the same tablet, YugabyteDB has optimizations to execute the transaction much faster. The transaction manager of YugabyteDB automatically detects transactions that update a single row (as opposed to transactions that update rows across tablets or nodes). In order to achieve high performance, the updates to a single row directly update the row without having to interact with the transaction status tablet using a single row transaction path (also known as fast path).

{{<lead link="single-row-transactions/">}}
To know more about single-row and single-shard transactions, see [Single-row transactions](single-row-transactions/).
{{</lead>}}

## Isolation levels

Isolation levels in databases refer to the degree of isolation between concurrent transactions, which determines how much each transaction is affected by the actions of others. YugabyteDB provides 3 isolation levels, Snapshot, Serializable, and Read Committed with the same isolation guarantees as PostgreSQL's `REPEATABLE READ`, `SERIALIZABLE` and `READ COMMITTED` respectively. Understanding and properly configuring the isolation levels in a database is crucial for ensuring data consistency and optimizing the performance of concurrent transactions.

{{<lead link="isolation-levels/">}}
To understand the different isolation levels and understand how they work, see [Transaction isolation levels](isolation-levels/).
{{</lead>}}

## Concurrency control

Concurrency control is a key mechanism in databases that ensures the correct and consistent execution of concurrent transactions. It is responsible for managing and coordinating multiple, simultaneous access to the same data to detect conflicts, maintain data integrity, and prevent anomalies. YugabyteDB uses two strategies for concurrency control, Fail-on-conflict and Wait-on-conflict.

{{<lead link="concurrency-control/">}}
To learn how YugabyteDB handles conflicts between concurrent transactions, see [Concurrency control](concurrency-control/).
{{</lead>}}

## Explicit locking

As with PostgreSQL, YugabyteDB provides various row-level lock modes to control concurrent access to data in tables. These modes can be used for application-controlled locking in cases where MVCC does not provide the desired behavior.

{{<lead link="../../explore/transactions/explicit-locking">}}
To learn about the different locking mechanism, see [Explicit locking](../../explore/transactions/explicit-locking).
{{</lead>}}

## Transaction priorities

Transaction priorities in databases refer to the order in which transactions are executed when there are conflicting operations or resource contention. The priorities are used to determine which transaction should be given preference when resolving conflicts. Some transactions could be aborted.

{{<lead link="transaction-priorities/">}}
To learn how YugabyteDB decides which transactions should be aborted in case of conflict, see [Transaction priorities](transaction-priorities/).
{{</lead>}}

## Read committed

Read Committed is the isolation level in which, clients do not need to retry or handle serialization errors (40001) in application logic.

{{<lead link="read-committed/">}}
To understand how Read committed is implement and how to use it, see [Read committed](read-committed/).
{{</lead>}}

## Read restart error

Read restart errors, also known as read skew or read consistency errors, are a type of concurrency control issue that can occur when using certain isolation levels. Although YugabyteDB has optimizations to resolve most scenarios automatically, depending on the level of clock skew, it can throw this error.

{{<lead link="read-restart-error/">}}
To understand when this error could be thrown, see [Read restart error](read-restart-error/)
{{</lead>}}.

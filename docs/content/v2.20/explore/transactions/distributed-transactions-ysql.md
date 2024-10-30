---
title: Distributed transactions in YSQL
headerTitle: Distributed transactions
linkTitle: Distributed transactions
description: Understand distributed transactions in YugabyteDB using YSQL.
headcontent:
menu:
  v2.20:
    name: Distributed transactions
    identifier: explore-transactions-distributed-transactions-1-ysql
    parent: explore-transactions
    weight: 230
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../distributed-transactions-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../distributed-transactions-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

The best way to understand distributed transactions in YugabyteDB is through examples.

To learn about how YugabyteDB handles failures during transactions, see [High availability of transactions](../../fault-tolerance/transaction-availability/).

{{% explore-setup-single %}}

## Create a table

Create the following table:

```sql
CREATE TABLE accounts (
  account_name varchar,
  account_type varchar,
  balance float,
  PRIMARY KEY (account_name, account_type)
);
```

Execute the following statements to insert sample data into the table:

```sql
INSERT INTO accounts VALUES ('John', 'savings', 1000);
INSERT INTO accounts VALUES ('John', 'checking', 100);
INSERT INTO accounts VALUES ('Smith', 'savings', 2000);
INSERT INTO accounts VALUES ('Smith', 'checking', 50);
```

Display the contents of the table, as follows:

```sql
yugabyte=# SELECT * FROM accounts;
```

```output
 account_name | account_type | balance
--------------+--------------+---------
 John         | checking     |     100
 John         | savings      |    1000
 Smith        | checking     |      50
 Smith        | savings      |    2000
(4 rows)
```

## Run a transaction

Run the following transaction:

```sql
BEGIN TRANSACTION;
  UPDATE accounts SET balance = balance - 200
      WHERE account_name='John' AND account_type='savings';
  UPDATE accounts SET balance = balance + 200
      WHERE account_name='John' AND account_type='checking';
COMMIT;
```

After the transaction succeeds, display the contents of the table:

```sql
yugabyte=# SELECT * FROM accounts;
```

```output
 account_name | account_type | balance
--------------+--------------+---------
 John         | checking     |     300
 John         | savings      |     800
 Smith        | checking     |      50
 Smith        | savings      |    2000
(4 rows)
```

The transaction can be broken down as follows:

1. Begin:

    ```sql
    BEGIN TRANSACTION;
    ```

    The node that receives this statement becomes the transaction coordinator. A new transaction record is created in the `transaction status` table for the current transaction. It has a unique transaction ID with the state `PENDING`. Note that in practice, these records are pre-created to achieve high performance.

1. Update:

    ```sql
    UPDATE accounts SET balance = balance - 200
      WHERE account_name='John'
      AND account_type='savings';
    ```

    The transaction coordinator writes a provisional record to the tablet that contains this row. The provisional record consists of the transaction ID, so the state of the transaction can be determined. If a provisional record written by another transaction already exists, then the current transaction would use the transaction ID that is present in the provisional record to fetch details and check if there is a potential conflict.

1. Second update:

    ```sql
    UPDATE accounts SET balance = balance + 200
      WHERE account_name='John'
      AND account_type='checking';
    ```

    This step is largely the same as the previous step. Note that the rows being accessed can be on different nodes. The transaction coordinator would need to perform a provisional write RPC to the appropriate node for each row.

1. Commit:

    ```sql
    COMMIT;
    ```

    To commit, all the provisional writes must have successfully completed. The `COMMIT` statement causes the transaction coordinator to update the transaction status in the `transaction status` table to `COMMITED`, at which point it is assigned the commit timestamp (a hybrid timestamp, to be precise). Now the transaction is completed. In the background, the `COMMIT` record along with the commit timestamp is applied to each of the rows that participated to make future lookups of these rows efficient.

The following diagram shows the sequence of events that occur when a transaction is running:

![Distributed transaction write path](/images/architecture/txn/distributed_txn_write_path.svg)

### Scalability

Because all nodes of the universe can process transactions by becoming transaction coordinators, horizontal scalability can be achieved by distributing the queries evenly across the nodes of the universe.

### Resilience

Each update performed as a part of the transaction is replicated across multiple nodes for resilience and persisted on disk. This ensures that the updates made as a part of a transaction that has been acknowledged as successful to the end user is resilient even if failures occur.

### Concurrency control

[Concurrency control](../../../architecture/transactions/concurrency-control/) in databases ensures that multiple transactions can execute concurrently while preserving data integrity. Concurrency control is essential for correctness in environments where two or more transactions can access the same data at the same time. The two primary mechanisms to achieve concurrency control are optimistic and pessimistic.

YugabyteDB currently supports optimistic concurrency control, with pessimistic concurrency control being worked on actively.

## Transaction options

To view options supported by transactions, execute the following `\h BEGIN` meta-command:

```sql
yugabyte=# \h BEGIN
```

```output
Command:     BEGIN
Description: start a transaction block
Syntax:
BEGIN [ WORK | TRANSACTION ] [ transaction_mode [, ...] ]

where transaction_mode is one of:

  ISOLATION LEVEL { SERIALIZABLE | REPEATABLE READ | READ COMMITTED | READ UNCOMMITTED }
  READ WRITE | READ ONLY
  [ NOT ] DEFERRABLE
```

### transaction_mode

You can set the `transaction_mode` to one of the following:

1. `READ WRITE` – You can perform read or write operations.
2. `READ ONLY` – You can only perform read operations.

For example, trying to do a write operation such as creating a table or inserting a row in a `READ ONLY` transaction would result in an error:

```sql
yugabyte=# BEGIN READ ONLY;
```

```output
BEGIN
```

```sql
yugabyte=# CREATE TABLE example(k INT PRIMARY KEY);
```

```output
ERROR: cannot execute CREATE TABLE in a read-only transaction
```

### DEFERRABLE transactions

As in PostgreSQL, the `DEFERRABLE` transaction property is not used unless the transaction is also both `SERIALIZABLE` and `READ ONLY`.

When all three of these properties (`SERIALIZABLE`, `DEFERRABLE`, and `READ ONLY`) are set for a transaction, the transaction may block when first acquiring its snapshot, after which it can run without the typical overhead of a `SERIALIZABLE` transaction and without any risk of contributing to or being canceled by a serialization failure.

This mode is well-suited for long-running reports or backups without impacting or being impacted by other transactions.
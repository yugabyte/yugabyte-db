---
title: Explicit locking
headerTitle: Explicit locking
linkTitle: Explicit locking
description: Explicit locking in YugabyteDB.
headcontent: Learn how row locking works in YugabyteDB
menu:
  preview:
    identifier: explore-transactions-explicit-locking-1-ysql
    parent: explore-transactions
    weight: 245
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../explicit-locking/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

</ul>

YugabyteDB's YSQL supports explicit row-level locking, similar to PostgreSQL. Explicit row-locks ensure that two transactions can never hold conflicting locks on the same row. When two transactions try to acquire conflicting lock modes, the semantics are dictated by YugabyteDB's [Concurrency Control](../../../architecture/transactions/concurrency-control/) policies.

The types of row locks currently supported are:

* `FOR UPDATE`
* `FOR NO KEY UPDATE`
* `FOR SHARE`
* `FOR KEY SHARE`

## Example

The following example uses the `FOR UPDATE` row lock with the [Fail-on-Conflict](../../../architecture/transactions/concurrency-control/#fail-on-conflict) concurrency control policy. First, a row is selected for update, thereby locking it, and subsequently updated. A concurrent transaction should not be able to abort this transaction by updating the value of that row after the row is locked.

{{% explore-setup-single %}}

To try out this scenario, first create an example table with sample data, as follows:

```sql
yugabyte=# CREATE TABLE t (k VARCHAR, v VARCHAR);
yugabyte=# INSERT INTO t VALUES ('k1', 'v1');
```

Next, connect to the cluster using two independent `ysqlsh` instances. You can connect both session `ysqlsh` instances to the same server, or to different servers.

Begin a transaction in the first session, and perform a `SELECT FOR UPDATE` on the row in the table `t`. This locks the row for an update as a part of a transaction that has a very high priority (i.e., in the `high priority bucket` as explained in [Transaction priorities](../../../architecture/transactions/transaction-priorities/)).

```sql
yugabyte=# BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
```

```output
BEGIN
```

```sql
yugabyte=# SELECT * from t WHERE k='k1' FOR UPDATE;
```

```output
 k  | v
----+----
 k1 | v1
(1 row)
```

Before completing the transaction, try to update the same key in your other session using a basic update statement.

```sql
yugabyte=# UPDATE t SET v='v1.1' WHERE k='k1';
```

```output
ERROR:  All transparent retries exhausted. Operation failed. Try again: bb3aace4-5de2-41f9-981e-d9ca06671419 Conflicts with higher priority transaction: d4dadbf8-ca81-4bbd-b68c-067023f8ee6b
```

This operation fails because it conflicts with the row-level lock and as per `Fail-on-Conflict` concurrency control policy, the transaction aborts itselfs since it has a lower priority.

Note that the error message appears after all [Best-effort statement retries](../../../architecture/transactions/concurrency-control/#best-effort-internal-retries-for-first-statement-in-a-transaction) have been exhausted.

Finally, in the first session, update the row and commit the transaction. This should succeed.

```sql
yugabyte=# UPDATE t SET v='v1.2' WHERE k='k1';
```

```output
UPDATE 1
```

```sql
yugabyte=# COMMIT;
```

```output
COMMIT
```

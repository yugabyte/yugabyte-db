---
title: Explicit Locking
headerTitle: Explicit Locking
linkTitle: Explicit Locking
description: Explicit Locking in YugabyteDB.
headcontent: Explicit Locking in YugabyteDB.
menu:
  v2.14:
    name: Explicit Locking
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

This section describes how explicit locking works in YugabyteDB.

YugabyteDB supports most row-level locks, similar to PostgreSQL. Explicit row-locks use transaction priorities to ensure that two transactions can never hold conflicting locks on the same row. To do this, the query layer acquires the row lock by assigning a very high value for the priority of the transaction that is being run. This causes all other transactions that conflict with the current transaction to fail, because they have a lower transaction priority.

{{< note title="Note" >}}
Explicit locking is an area of active development in YugabyteDB. A number of enhancements are planned in this area. Unlike PostgreSQL, YugabyteDB uses optimistic concurrency control and does not block / wait for currently held locks, instead opting to abort the conflicting transaction with a lower priority. Pessimistic concurrency control is currently under development.
{{</note >}}

The types of row locks currently supported are:

* `FOR UPDATE`
* `FOR NO KEY UPDATE`
* `FOR SHARE`
* `FOR KEY SHARE`

The following example uses the `FOR UPDATE` row lock. First, a row is selected for update, thereby locking it, and subsequently updated. A concurrent transaction should not be able to abort this transaction by updating the value of that row after the row is locked.

To try out this scenario, first create an example table with sample data, as follows:

```sql
yugabyte=# CREATE TABLE t (k VARCHAR, v VARCHAR);
yugabyte=# INSERT INTO t VALUES ('k1', 'v1');
```

Next, connect to the cluster using two independent `ysqlsh` instances. You can connect both session `ysqlsh` instances to the same server, or to different servers.

Begin a transaction in the first session, and perform a `SELECT FOR UPDATE` on the row in the table `t`. This locks the row for an update as a part of a transaction that has a very high priority.

```sql
yugabyte=# BEGIN;
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

Before completing the transaction, try to update the same key in your other session using a simple update statement.

```sql
yugabyte=# UPDATE t SET v='v1.1' WHERE k='k1';
```

```output
ERROR:  Operation failed. Try again. xxx Conflicts with higher priority transaction: yyy
```

This uses optimistic concurrency control, and fails.

If you used optimistic concurrency control instead of an explicit row-lock to do the first transaction, then this update would succeed in some of the attempts and the first transaction would fail in those cases.

{{< note title="Note" >}}
Blocking this transaction and retrying it after the other transaction completes is work in progress.
{{</note >}}

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

---
title: Explicit locking
headerTitle: Explicit locking
linkTitle: Explicit locking
description: Explicit locking in YugabyteDB.
headcontent: Row locking and advisory locks in YugabyteDB
menu:
  stable:
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

## Row-level locks

YugabyteDB's YSQL supports explicit row-level locking, similar to PostgreSQL. Explicit row-locks ensure that two transactions can never hold conflicting locks on the same row. When two transactions try to acquire conflicting lock modes, the semantics are dictated by YugabyteDB's [concurrency control](../../../architecture/transactions/concurrency-control/) policies.

The following types of row locks are supported:

- FOR UPDATE
- FOR NO KEY UPDATE
- FOR SHARE
- FOR KEY SHARE

The following example uses the FOR UPDATE row lock with the [fail-on-conflict](../../../architecture/transactions/concurrency-control/#fail-on-conflict) concurrency control policy. First, a row is selected for update, thereby locking it, and subsequently updated. A concurrent transaction should not be able to abort this transaction by updating the value of that row after the row is locked.

{{% explore-setup-single-new %}}

Create a sample table and populate it with sample data, as follows:

```sql
yugabyte=# CREATE TABLE t (k VARCHAR, v VARCHAR);
yugabyte=# INSERT INTO t VALUES ('k1', 'v1');
```

Next, connect to the universe using two independent ysqlsh instances. You can connect both session ysqlsh instances to the same server or to different servers.

Begin a transaction in the first session and perform a SELECT FOR UPDATE on the row in the table `t`. This locks the row for an update as a part of a transaction that has a very high priority (that is, in the `high priority bucket`, as explained in [Transaction priorities](../../../architecture/transactions/transaction-priorities/)):

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

Before completing the transaction, try to update the same key in your other session using a basic update statement, as follows:

```sql
yugabyte=# UPDATE t SET v='v1.1' WHERE k='k1';
```

```output
ERROR:  All transparent retries exhausted. Operation failed. Try again: bb3aace4-5de2-41f9-981e-d9ca06671419 Conflicts with higher priority transaction: d4dadbf8-ca81-4bbd-b68c-067023f8ee6b
```

This operation fails because it conflicts with the row-level lock and as per Fail-on-Conflict concurrency control policy, the transaction aborts itself because it has a lower priority.

Note that the error message appears after all [best-effort statement retries](../../../architecture/transactions/concurrency-control/#best-effort-internal-retries-for-first-statement-in-a-transaction) have been exhausted.

Finally, in the first session, update the row and commit the transaction, as follows:

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

This should succeed.

## Advisory locks

Advisory locks are available in {{<release "2.25.1.0">}} and later.

YSQL also supports advisory locks, where the application manages concurrent access to resources through a cooperative locking mechanism. Advisory locks can be less resource-intensive than table or row locks for certain use cases because they don't involve scanning tables or indexes for lock conflicts. They are session-specific and managed by the client application.

In PostgreSQL, if an advisory lock is taken on one session, all sessions should be able to see the advisory locks acquired by any other session. Similarly, in YugabyteDB, if an advisory lock is acquired on one session, all the sessions should be able to see the advisory locks regardless of the node the session is connected to. This is achieved via the pg_advisory_locks system table, which is dedicated to hosting advisory locks. All advisory lock requests are stored in this system table.

To enable the use of advisory locks in a cluster, you must set the [Advisory lock flags](../../../reference/configuration/yb-tserver/#advisory-lock-flags).

### Using advisory locks

Advisory locks in YugabyteDB are semantically identical to PostgreSQL, and are managed using the same functions. Refer to [Advisory lock functions](https://www.postgresql.org/docs/15/functions-admin.html#FUNCTIONS-ADVISORY-LOCKS) in the PostgreSQL documentation.

You can acquire an advisory lock in the following ways:

- Session level

    Once acquired at session level, the advisory lock is held until it is explicitly released or the session ends.

    Unlike standard lock requests, session-level advisory lock requests do not honor transaction semantics: a lock acquired during a transaction that is later rolled back will still be held following the rollback, and likewise an unlock is effective even if the calling transaction fails later. A lock can be acquired multiple times by its owning process; for each completed lock request there must be a corresponding unlock request before the lock is actually released.

    ```sql
    SELECT pg_advisory_lock(10);
    ```

- Transaction level

    Transaction-level lock requests, on the other hand, behave more like regular row-level lock requests: they are automatically released at the end of the transaction, and there is no explicit unlock operation. This behavior is often more convenient than the session-level behavior for short-term usage of an advisory lock.

    ```sql
    SELECT pg_advisory_xact_lock(10);
    ```

Advisory locks can also be exclusive or shared:

- Exclusive Lock

    Only one session/transaction can hold the lock at a time. Other sessions/transactions can't acquire the lock until the lock is released.

    ```sql
    select pg_advisory_lock(10);
    select pg_advisory_xact_lock(10);
    ```

- Shared Lock

    Multiple sessions/transactions can hold the lock simultaneously. However, no session/transaction can acquire an exclusive lock while shared locks are held.

    ```sql
    select pg_advisory_lock_shared(10);
    select pg_advisory_xact_lock_shared(10);
    ```

Finally, advisory locks can be blocking or non-blocking:

- Blocking lock

    The process trying to acquire the lock waits until the lock is acquired.

    ```sql
    select pg_advisory_lock(10);
    select pg_advisory_xact_lock(10);
    ```

- Non-blocking lock

    The process immediately returns a boolean value stating if the lock is acquired or not.

    ```sql
    select pg_try_advisory_lock(10);
    select pg_try_advisory_xact_lock(10);
    ```

## Table-level locks

{{<tags/feature/tp idea="1114">}} Table-level locks are available in {{<release "2025.1.1.0">}} and later.

YugabyteDB's YSQL supports table-level locks (also known as object locks) to coordinate between DML and DDL operations. This feature ensures that DDLs wait for in-progress DMLs to finish before making schema changes, and gates new DMLs behind any waiting DDLs, providing concurrency handling that closely matches PostgreSQL behavior.

You can enable Table-level locks under a preview flag, `enable_object_locking_for_table_locks`.

Table-level locks depend on:

- **YSQL lease**: Controlled by `master_ysql_operation_lease_ttl_ms`
- **DDL Atomicity**: Controlled by `ysql_enable_db_catalog_version_mode`

PostgreSQL table locks can be broadly categorized into two types:

**Shared locks:**

- `ACCESS SHARE`
- `ROW SHARE`
- `ROW EXCLUSIVE`

**Global locks:**

- `SHARE UPDATE EXCLUSIVE`
- `SHARE`
- `SHARE ROW EXCLUSIVE`
- `EXCLUSIVE`
- `ACCESS EXCLUSIVE`

DMLs acquire shared locks alone, while DDLs acquire a combination of shared and global locks.

To reduce the overhead of DMLs, YugabyteDB takes shared locks on the PostgreSQL backend's host TServer alone. Global locks are propagated to the Master leader.

Each TServer maintains an in-memory `TSLocalLockManager` that serves object lock acquire or release calls. The Master also maintains an in-memory lock manager primarily for serializing conflicting global object locks. When the Master leader receives a global lock request, it first acquires the lock locally, then fans out the lock request to all TServers with valid [YSQL leases], and executes the client callback after the lock has been successfully acquired on all TServers (with a valid YSQL lease).

### Using table-level locks

All statements inherently acquire some object locks. You can also explicitly acquire object locks using the [LOCK TABLE API](https://www.postgresql.org/docs/current/sql-lock.html):

```sql
-- Acquire a share lock on a table
LOCK TABLE my_table IN SHARE MODE;

-- Acquire an access exclusive lock (blocks all other operations)
LOCK TABLE my_table IN ACCESS EXCLUSIVE MODE;
```

### Lock scope and lifecycle

All object locks are tied to a DocDB transaction:

- **DMLs**: Locks are released at commit or abort time
- **DDLs**: Lock release is delegated to the Master, which has a background task for observing DDL commits or aborts and finalizing schema changes.

When a DDL finishes, all locks corresponding to the transaction are released and the TServers have the latest catalog cache. This ensures any new DMLs waiting on the same locks to see the latest schema after acquiring the object locks.

To reduce overhead for read-only workloads, YugabyteDB reuses DocDB transactions wherever possible.

### Observability

You can observe active object locks using the `pg_locks` system view for locks older than `yb_locks_min_txn_age`:

```sql
SELECT * FROM pg_locks WHERE NOT granted;
```

### Failure handling

Locks are cleaned up for various failure scenarios as follows:

- PostgreSQL backend crash: The TServer-Pos session heartbeat cleans up corresponding locks
- **TServer crash**: Lease mechanism cleans up DDL locks for that TServer
- **Master leader crash**: Newly elected master leader replays catalog entries to recreate lock state

### Important considerations

- If a TServer doesn't have a valid YSQL lease, it cannot serve any requests from pg backends
- Upon TServer lease changes (due to network issues), all active pg backends are killed
- If TServers cannot communicate with the master leader, DDLs will stall since global locks cannot be served
- If TServers cannot communicate with the master longer than the YSQL lease timeout, they lose their YSQL lease and all pg backends are killed
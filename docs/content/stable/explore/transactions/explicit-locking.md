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

YSQL also supports advisory locks, where the application manages concurrent access to resources through a cooperative locking mechanism. Advisory locks can be less resource-intensive than table or row locks for certain use cases because they don't involve scanning tables or indexes for lock conflicts. They are session-specific and managed by the client application.

In PostgreSQL, if an advisory lock is taken on one session, all sessions should be able to see the advisory locks acquired by any other session. Similarly, in YugabyteDB, if an advisory lock is acquired on one session, all the sessions should be able to see the advisory locks regardless of the node the session is connected to. This is achieved via the pg_advisory_locks system table, which is dedicated to hosting advisory locks. All advisory lock requests are stored in this system table.

Advisory locks are enabled by default. You can configure advisory locks using the [Advisory lock flags](../../../reference/configuration/yb-tserver/#advisory-lock-flags).

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

Table-level locks depend on:

- [Transactional DDL](../transactional-ddl/), controlled by [ysql_yb_ddl_transaction_block_enabled](../transactional-ddl/#enable-transactional-ddl) (preview flag).
- [YSQL lease](../../../architecture/transactions/concurrency-control/#ysql-lease-mechanism), lease period controlled by [master_ysql_operation_lease_ttl_ms](../../../reference/configuration/yb-master/#master-ysql-operation-lease-ttl-ms).
- Per-database catalog caching, controlled by [ysql_enable_db_catalog_version_mode](../../../reference/configuration/yb-master/#ysql-enable-db-catalog-version-mode).

Table-level locking provides serializable semantics between DMLs and DDLs for YSQL by introducing distributed locks on YSQL objects. PostgreSQL clients acquire locks to prevent DMLs and DDLs from running concurrently. DML locks are acquired only on the TServer hosting the PostgreSQL session, but DDL locks are acquired on every TServer in the universe to ensure no DML touching the locked object runs while the DDL does.

To prevent dead TServers holding locks from permanently blocking subsequent DMLs or DDLs, YugabyteDB internally uses the [YSQL lease mechanism](../../../architecture/transactions/concurrency-control/#ysql-lease-mechanism) between TServers and the Master leader to serve any YSQL DMLs. All locks held by a TServer are released when its lease expires.

### Enable table-level locks

Table-level locks are disabled by default. To enable the feature, set the [yb-tserver](../../../reference/configuration/yb-tserver/) flag `enable_object_locking_for_table_locks` to true.

Because `enable_object_locking_for_table_locks` is a preview flag, to use it, add the flag to the [allowed_preview_flags_csv](../../../reference/configuration/yb-tserver/#allowed-preview-flags-csv) list (that is, `allowed_preview_flags_csv=enable_object_locking_for_table_locks`).

As the table-level locks feature depends on Transactional DDL (currently not enabled by default), you need to enable the preview flag, [ysql_yb_ddl_transaction_block_enabled](../transactional-ddl/#enable-transactional-ddl).

For more information on the lock scopes and lifecycle, see [Table-level locks](../../../architecture/transactions/concurrency-control/#table-level-locks).

---
title: Get lock information insights with pg_locks
linkTitle: Lock insights
description: Using pg_locks to get insights into lock information specific to YugabyteDB's distributed SQL architecture.
headerTitle: Get insights on transaction locks with pg_locks
menu:
  v2.25:
    identifier: pg-locks
    parent: explore-observability
    weight: 410
type: docs
---

YugabyteDB supports the PostgreSQL [pg_locks](https://www.postgresql.org/docs/current/view-pg-locks.html) system view, which provides information about the locks held and requested by the current active transactions in a database. The `pg_locks` view is tailored to YugabyteDB's lock handling mechanisms, providing a comprehensive overview of database lock states. YugabyteDB also provides two additional fields, `waitend` and `ybdetails`, which provide insights into lock information specific to YugabyteDB's distributed architecture.

## Scenarios

The `pg_locks` view is used in diagnosing and resolving locking and contention issues in a YugabyteDB cluster. Use this view to diagnose and troubleshoot problem transactions, including the following:

- Display long-held locks. Identify transactions that have been holding locks for an extended period, potentially indicating issues with lock contention.
- Filter results. Narrow down the lock information to specific tables or transactions for targeted analysis.
- Diagnose stuck sessions. Understand which transactions are blocking a session so you can troubleshoot application delays or deadlocks.
- User intervention. Use the view to cancel transactions that are causing lock contention, and free up resources for other transactions.

The following table describes the view columns:

| Column | Type | Description |
| :---- | :--- | :---------- |
| locktype | text | The type of the lockable object. Valid types are relation, keyrange, key, and column. |
| database | oid | Object identifier (OID) of the database to  where the lock target exists. |
| relation | oid | OID of the relation targeted by the lock. |
| pid | pid | Process identifier (PID) of the backend holding the lock. |
| mode | text | The lock modes held or desired. Valid modes are WEAK_READ and WEAK_WRITE. |
| granted | boolean | Indicates if the lock is held (true) or awaited (false). |
| fastpath | boolean | True for [single row operations](../../../architecture/transactions/single-row-transactions/) that operate on a single tablet, don't need a transaction, and do not take locks. (They take the _fast path_ by writing directly to the database.)  |
| waitstart | timestampz | Time at which a YB-TServer starts waiting for this lock. |
| waitend | timestampz | Time at which a lock gets acquired. |
| ybdetails | JSONB | Field with details specific to YugabyteDB locks, including `node`, `transactionid`, and `blocked_by details`.|

PostgreSQL-specific fields, including `page`, `tuple`, `virtualxid`, `transactionid`, `classid`, `objid`, `objsubid`, and `virtualtransaction`, are not applicable in YugabyteDB and are always NULL.

## YugabyteDB-specific changes

The `pg_locks` view includes the following YugabyteDB-specific fields.

### waitend

The timestamp at which the lock was acquired by a transaction (PostgtreSQL does not currently record this timestamp).

### ybdetails

JSONB type that encapsulates additional information about each lock, specific to YugabyteDB's distributed architecture, and includes the following attributes:

- `node`: The YB-TServer UUID of the node hosting the PostgreSQL backend that is holding the lock. Identifies the specific node in the YugabyteDB cluster where the lock is being held, which allows you to troubleshoot and monitor lock distribution across nodes.
- `transactionid`: The UUID of the YugabyteDB transaction ID owning this lock. This field is NULL for single shard or fastpath operations, offering visibility into the transactional context of the lock. Use this field to track and manage transactions across the distributed database.
- `subtransaction_id`: The ID of the subtransaction in which the lock was acquired.
- `is_explicit`: True when the lock was acquired explicitly, such as through FOR UPDATE, FOR NO KEY UPDATE, FOR SHARE, FOR KEY SHARE, and so on. This attribute helps distinguish between locks acquired automatically by the database system and those requested explicitly by you, aiding in lock analysis and optimization efforts.
- `tablet_id`: The ID of the tablet containing this lock. In YugabyteDB, data is sharded into tablets, and this ID helps identify the specific shard where the lock exists, crucial for diagnosing sharding-related lock contention issues.
- `blocked_by`: A list of transactions blocking the acquisition of this lock. This field is helpful in identifying deadlock scenarios, and determining which transaction is blocking other operations from moving forward.
- `keyrangedetails`: Provides details about keys that the locks hold, including the following:

  - `cols`: A list of column values from the PRIMARY KEY of the table, offering insight into the exact row(s) or key range that is locked.
  - `attnum`: The PostgreSQL attribute number indicating if the lock is a column-level lock, linking the lock to the specific table column.
  - `column_id`: The column ID in DocDB if the lock is a column-level lock, further aligning lock information with YugabyteDB's internal document-oriented architecture.
  - `multiple_rows_locked`: Indicates when the lock is held on more than one entry in DocDB, helping to understand the scope of the lock in the database's document model.

## Filter your results

Use the following [YSQL configuration parameters](../../../reference/configuration/yb-tserver/#ysql-configuration-parameters) to customize how locks are queried and displayed, so you can tailor the lock information as needed.

| Parameter | Description |
| :--- | :--- |
| yb_locks_min_txn_age | Sets the minimum age of a transaction (in seconds) before its locks are included in the results returned from querying pg_locks. Use this parameter to focus on older transactions that may be more relevant to performance tuning or deadlock resolution efforts. Transactions that are started more recently than the specified duration are not shown, helping to reduce clutter and focus on potentially problematic transactions. |
| yb_locks_max_transactions | The maximum number of transactions for which lock information is displayed when you query pg_locks. Use this to limit the output to the most relevant transactions in environments with high levels of concurrency and transactional activity. |
| yb_locks_txn_locks_per_tablet | Sets the maximum number of rows per transaction per tablet to return in pg_locks. Set to 0 to return all results. |

## Examples

{{% explore-setup-single-new %}}

The following examples show how you can use the pg_locks view in YugabyteDB:

- To display long-held locks, run the following command:

    ```sql
    SET session yb_locks_min_txn_age = 5000;
    SELECT * FROM pg_locks;
    ```

- To change the maximum number of transactions to display to 10, run the following command:

    ```sql
    SET session yb_locks_max_transactions = 10;
    SELECT * FROM pg_locks;
    ```

- To return a maximum of 10 rows per transaction per tablet, run the following command:

    ```sql
    SET session yb_locks_txn_locks_per_tablet = 10;
    SELECT * FROM pg_locks;
    ```

- To filter results for a specific table, run the following command:

    ```sql
    SELECT * FROM pg_locks WHERE relation = 'user_app.products'::regclass;
    ```

- To find locks by transaction ID, run the following command:

    ```sql
    SELECT * FROM pg_locks WHERE ybdetails->>'transactionid' = '{yb_txn_id}';
    ```

- To diagnose blocked sessions, run the following command:

    ```sql
    SELECT * FROM pg_locks
    WHERE ybdetails->>'transaction_id' IN
    (SELECT yb_transaction_id FROM pg_stat_activity WHERE pid = <blocked_pid>);
    ```

- To identify blocked sessions, run the following command:

    ```sql
    SELECT * FROM pg_locks WHERE granted = false;
    ```

- To cancel a transaction, run the following command:

    ```sql
    SELECT yb_cancel_transaction('{yb_txn_id}');
    ```

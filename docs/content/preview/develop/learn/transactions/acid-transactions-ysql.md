---
title: ACID Transactions in YSQL
headerTitle: Transactions in YSQL
linkTitle: Transactions
description: Learn how to use Transactions in YSQL on YugabyteDB.
aliases:
  - /preview/explore/transactional/acid-transactions/
  - /preview/develop/learn/acid-transactions/
  - /preview/develop/learn/acid-transactions-ysql/
menu:
  preview:
    identifier: acid-transactions-1-ysql
    parent: learn
    weight: 140
rightNav:
  hideH4: true
type: docs
---

{{<tabs>}}
{{<tabitem href="../acid-transactions-ysql/" text="YSQL" icon="postgres" active="true" >}}
{{<tabitem href="../acid-transactions-ycql/" text="YCQL" icon="cassandra" >}}
{{</tabs>}}

In YugabyteDB, a transaction is a sequence of operations performed as a single logical unit of work. The essential point of a transaction is that it bundles multiple steps into a single, all-or-nothing operation. The intermediate states between the steps are not visible to other concurrent transactions, and if some failure occurs that prevents the transaction from completing, then none of the steps affect the database at all.

## Overview

In YugabyteDB, a transaction is the set of commands inside a [BEGIN](../../../../api/ysql/the-sql-language/statements/txn_begin/) - [COMMIT](../../../../api/ysql/the-sql-language/statements/txn_commit/) block. For example:

```plpgsql
BEGIN;
  UPDATE accounts SET balance = balance + 1000.00 WHERE name = 'John Smith';
  -- other statements
COMMIT;
```

The `BEGIN` and `COMMIT` block is needed when you have multiple statements to be executed as part of a transaction. YugabyteDB treats every ad-hoc individual SQL statement as being executed in a transaction.

If you decide to cancel the transaction and not commit it, you can issue a `ROLLBACK` instead of a `COMMIT`. You can also control the rollback of a subset of statements using `SAVEPOINT`. After rolling back to a savepoint, it continues to be defined, so you can roll back to it several times.

As all transactions in YugabyteDB are guaranteed to be [ACID](../../../../architecture/transactions/transactions-overview/) compliant, errors can be thrown during transaction processing to ensure correctness guarantees are not violated. YugabyteDB returns different [error codes](../transactions-errorcodes-ysql/) for each case with details. Applications need to be designed to do [retries](../transactions-retries-ysql/) correctly for high availability.

{{<tip title="In action">}}
For an example of how a transaction is run, see [Distributed transactions](../../../../explore/transactions/distributed-transactions-ysql/).
{{</tip>}}

## Typical commands

The following commands are typically involved in a transaction flow:

| Command | Description | Example |
| :------ | :---------- | :------ |
|[BEGIN](../../../../api/ysql/the-sql-language/statements/txn_begin/) | Start a transaction. This is the first statement in a transaction. | *BEGIN TRANSACTION* |
|[SET](../../../../api/ysql/the-sql-language/statements/cmd_set/) | Set session-level transaction settings | SET idle_in_transaction_session_timeout = 10000 |
|[SHOW](../../../../api/ysql/the-sql-language/statements/cmd_show/) | Display session-level transaction settings | SHOW idle_in_transaction_session_timeout |
|[SET TRANSACTION](../../../../api/ysql/the-sql-language/statements/txn_set/) | Set the isolation level. | SET TRANSACTION SERIALIZABLE |
|[SAVEPOINT](../../../../api/ysql/the-sql-language/statements/savepoint_create/) | Create a checkpoint. | SAVEPOINT yb_save|
|[ROLLBACK TO SAVEPOINT](../../../../api/ysql/the-sql-language/statements/savepoint_rollback/) | Rollback to a specific savepoint. | ROLLBACK TO SAVEPOINT yb_save |
|[RELEASE SAVEPOINT](../../../../api/ysql/the-sql-language/statements/savepoint_release/) | Destroy a savepoint. | RELEASE yb_save |
|[ROLLBACK](../../../../api/ysql/the-sql-language/statements/txn_rollback/) | Cancel a transaction. | ROLLBACK |
|[COMMIT](../../../../api/ysql/the-sql-language/statements/txn_commit/) | Apply the transaction to the tables. | COMMIT |

{{<tip title="Settings">}}
For an overview of what settings can be set for a transaction, see [Session-level settings](#session-level-settings).
{{</tip>}}

## Concurrency control

### Isolation levels

The isolation level defines the level of data visibility to the transaction. YugabyteDB supports [multi-version concurrency control (MVCC)](../../../../architecture/transactions/transactions-overview/#multi-version-concurrency-control), which enables the isolation of concurrent transactions without the need for locking.

YugabyteDB supports three kinds of isolation levels to support different application needs.

| Level | Description |
| :---- | :---------- |
| [Repeatable&nbsp;Read (Snapshot)](../../../../explore/transactions/isolation-levels/#snapshot-isolation) | Only the data that is committed before the transaction began is visible to the transaction. Effectively, the transaction sees the snapshot of the database as of the start of the transaction. {{<note>}}Applications using this isolation level should be designed to [retry](../transactions-retries-ysql#client-side-retry) on serialization failures.{{</note>}} |
| [Read Committed](../../../../explore/transactions/isolation-levels/#read-committed-isolation){{<badge/ea>}} | Each statement of the transaction sees the latest data committed by any concurrent transaction just before the execution of the statement. If another transaction has modified a row related to the current transaction, the current transaction waits for the other transaction to commit or rollback its changes. {{<note>}} The server internally waits and retries on conflicts, so applications [need not retry](../transactions-retries-ysql#automatic-retries) on serialization failures.{{</note>}} |
| [Serializable](../../../../explore/transactions/isolation-levels/#serializable-isolation) | This is the strictest isolation level and has the effect of all transactions being executed in a serial manner, one after the other rather than in parallel. {{<note>}} Applications using this isolation level should be designed to [retry](../transactions-retries-ysql/#client-side-retry) on serialization failures.{{</note>}} |

{{<tip title="Examples">}}
See [isolation level examples](../../../../explore/transactions/isolation-levels/) to understand the effect of these different levels of isolation.
{{</tip>}}

### Explicit locking

Typically [SELECT](../../../../api/ysql/the-sql-language/statements/dml_select) statements do not automatically lock the rows fetched during a transaction. Depending on your application needs, you might have to lock the rows retrieved during SELECT. YugabyteDB supports [explicit row-level locking](../../../../explore/transactions/explicit-locking) for such cases and ensures that no two transactions can hold locks on the same row. Lock acquisition conflicts are resolved according to [concurrency control](../../../../architecture/transactions/concurrency-control/) policies.

Lock acquisition has the following format:

```plpgsql
SELECT * FROM txndemo WHERE k=1 FOR UPDATE;
```

YugabyteDB supports the following types of explicit row locks:
| Lock | Description |
| :--- | :---------- |
| **FOR UPDATE** | Strongest and exclusive lock. Prevents all other locks on these rows till the transaction ends.|
| **FOR&nbsp;NO&nbsp;KEY&nbsp;UPDATE** | Weaker than `FOR UPDATE` and exclusive. Will not block `FOR KEY SHARE` commands.|
| **FOR SHARE** | Shared lock that does not block other `FOR SHARE` and `FOR KEY SHARE` commands.|
| **FOR KEY SHARE** | Shared lock that does not block other `FOR SHARE`, `FOR KEY SHARE`, and `FOR NO KEY UPDATE` commands.|

{{<tip title="Examples">}}
For more details and examples related to these locking policies, see [Explicit locking](../../../../explore/transactions/explicit-locking/).
{{</tip>}}

## Retry on failures

During transaction processing, failures can happen due to the strong [ACID](../../../../architecture/transactions/transactions-overview/) properties guaranteed by YugabyteDB. Appropriate [error codes](../transactions-errorcodes-ysql/) are returned for each scenario and applications should adopt the right [retry mechanisms](../transactions-retries-ysql/) specific to the isolation levels it uses to be highly available. In general, the error codes can be classified into the following three types:

1. WARNING. Informational messages that explain why a statement failed. For example:

    ```output
    -- When a BEGIN statement is issued inside a transaction
    WARNING:  25001: there is already a transaction in progress
    ```

    Most client libraries hide warnings, but you might notice the messages when you execute statements directly from a terminal. The statement execution can continue without interruption but would need to be modified to avoid the re-occurrence of the message.

1. ERROR: Errors are returned when a transaction cannot continue and has to be restarted by the client. For example:

    ```output
    -- When multiple transactions are modifying the same key.
    ERROR:  40001: Operation expired: Transaction XXXX expired or aborted by a conflict
    ```

    These errors need to be handled by the application to take appropriate action.

1. FATAL. Fatal messages are returned to notify that the connection to a server has been disconnected. For example:

    ```output
    -- When the application takes a long time to issue a statement in the middle of a transaction.
    FATAL:  25P03: terminating connection due to idle-in-transaction timeout
    ```

    At this point, the application should reconnect to the server.

{{<tip>}}
For more details on how to handle failures and retry, see [Transaction retries](../transactions-retries-ysql/).

For an example application and try it out yourself, see [Designing a Retry Mechanism for Resilient Spring Boot Applications](https://www.yugabyte.com/blog/retry-mechansim-spring-boot-app/).
{{</tip>}}

## Tuning for high performance

All applications need to be tuned to get the best performance. YugabyteDB supports various constructs and [multiple settings](../transactions-performance-ysql/) that can be adopted and tuned to your needs. Adopting the correct constructs in the right scenarios can immensely improve the performance of your application. Some examples are:

- Convert a multi-statement transaction affecting a single row into a [fast-path](../transactions-performance-ysql/#fast-single-row-transactions) transaction.
- [Avoid long waits](../transactions-performance-ysql/#avoid-long-waits) with the right timeouts.
- [Minimize conflict errors](../transactions-performance-ysql/#minimize-conflict-errors) with `ON CONFLICT` clause.
- [Uninterrupted long scans](../transactions-performance-ysql/#large-scans-and-batch-jobs)
- [Minimize round trips](../transactions-performance-ysql/#stored-procedures-minimize-round-trips) with stored procedures.

{{<lead link="../transactions-performance-ysql/">}}
For more examples and details on how to tune your application's performance, see [Performance tuning](../transactions-performance-ysql/).
{{</lead>}}

## Observability

YugabyteDB exports a lot of [observable metrics](../../../../explore/observability/) so that you can see what is going on in your cluster. These metrics can be exported to [Prometheus](../../../../explore/observability/prometheus-integration/macos/) and visualized in [Grafana](../../../../explore/observability/grafana-dashboard/grafana/). Many of these metrics are also displayed as charts in YugabyteDB Anywhere and YugabyteDB Managed. The following are key transaction-related metrics.

##### transactions_running

Shows the number of transactions that are currently active. This provides an overview of how transaction intensive the cluster currently is.

##### transaction_conflicts

Describes the number of times transactions have conflicted with other transactions. An increase in the number of conflicts could directly result in increased latency of your applications.

##### expired_transactions

Shows the number of transactions that did not complete because the status tablet did not receive enough heartbeats from the node to which the client had connected. This usually happens if that node or process managing the transaction has crashed.

## Session-level settings

The following YSQL parameters affect transactions and can be configured to your application needs. These settings can be set using the [SET](../../../../api/ysql/the-sql-language/statements/cmd_set/) command and the current values can be fetched using the [SHOW](../../../../api/ysql/the-sql-language/statements/cmd_show/) command.

{{<note title="Note">}}
These settings impact all transactions in the current session only.
{{</note>}}

##### default_transaction_read_only

Turn this setting `ON/TRUE/1` to make all the transactions in the current session read-only. This is helpful when you want to run reports or set up [follower reads](../transactions-global-apps/#read-from-followers).

```plpgsql
SET default_transaction_read_only = TRUE;
```

##### default_transaction_isolation

Set this to one of `serializable`, `repeatable read`, or `read committed`. This sets the default isolation level for all transactions in the current session.

```plpgsql
SET default_transaction_isolation = 'serializable';
```

##### default_transaction_deferrable

Turn this setting `ON/TRUE/1` to make all the transactions in the current session [deferrable](../../../../api/ysql/the-sql-language/statements/txn_set/#deferrable-mode-1). This ensures that the transactions are not canceled by a serialization failure.

```plpgsql
SET default_transaction_deferrable = TRUE;
```

{{<note title="Note">}}
The `DEFERRABLE` transaction property has no effect unless the transaction is also `SERIALIZABLE` and `READ ONLY`.
{{</note>}}

##### idle_in_transaction_session_timeout

Set this to a duration (for example, `'10s or 1000'`) to limit delays in transaction statements. The default time unit is milliseconds. See [Handle idle transactions](../transactions-performance-ysql/#handle-idle-applications).

##### yb_transaction_priority_lower_bound

Set this to values in the range `[0.0 - 1.0]` to set the lower bound of the dynamic priority assignment. See [Optimistic concurrency control](../transactions-performance-ysql/#optimistic-concurrency-control).

##### yb_transaction_priority_upper_bound

Set this to values in the range `[0.0 - 1.0]` to set the upper bound of the dynamic priority assignment. See [Optimistic concurrency control](../transactions-performance-ysql/#optimistic-concurrency-control).

## Learn more

- [Transaction error codes](../transactions-errorcodes-ysql/) - Various error codes returned during transaction processing.
- [Transaction error handling](../transactions-retries-ysql/) - Methods to handle various error codes to design highly available applications.
- [Transaction isolation levels](../../../../architecture/transactions/isolation-levels/) - Various isolation levels supported by YugabyteDB.
- [Concurrency control](../../../../architecture/transactions/concurrency-control/) - Policies to handle conflicts between transactions.
- [Transaction priorities](../../../../architecture/transactions/transaction-priorities/) - Priority buckets for transactions.
- [Transaction options](../../../../explore/transactions/distributed-transactions-ysql/#transaction-options) - Options supported by transactions.

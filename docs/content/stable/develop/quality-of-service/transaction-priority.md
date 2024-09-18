---
title: Transaction priorities
headerTitle: Transaction priorities
linkTitle: Transaction priorities
description: Transaction priorities in YugabyteDB.
headcontent: Transaction priorities in YugabyteDB
menu:
  stable:
    name: Transaction priorities
    identifier: develop-quality-of-service-transaction-priorities
    parent: develop-quality-of-service
    weight: 235
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../transaction-priority/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

</ul>

When using YugabyteDB with the [Fail-on-Conflict](../../../architecture/transactions/concurrency-control/#fail-on-conflict) concurrency control policy, higher priority transactions can abort lower priority transactions when conflicts occur. External applications may control the priority of individual transactions using the YSQL parameters `yb_transaction_priority_lower_bound` and `yb_transaction_priority_upper_bound`.

A random number between the lower and upper bound is picked and used to compute a transaction priority for the transactions in that session as explained in [Transaction Priorities](../../../architecture/transactions/transaction-priorities/).

| Flag | Valid Range | Description |
| :--- | :---------- | :---------- |
| `yb_transaction_priority_lower_bound` | Any value between 0 and 1, lower than the upper bound | Minimum transaction priority for transactions run in this session |
| `yb_transaction_priority_upper_bound` | Any value between 0 and 1, higher than the lower bound | Maximum transaction priority for transactions run in this session |

To view the transaction priority of the active transaction in current session, use the `yb_get_current_transaction_priority` function.

{{< note title="Note" >}}
Currently, transaction priorities work in the following scenarios:

* Works with YSQL only, not supported for YCQL.
* Only applies for transactions using [Fail-on-Conflict](../../../architecture/transactions/concurrency-control/#fail-on-conflict) concurrency control policy.
* Only conflict resolution is prioritized, not resource consumption as a part.

{{< /note >}}

## Examples

Create a [YugabyteDB universe](/preview/quick-start/) and open two separate [ysqlsh](../../../admin/ysqlsh/#starting-ysqlsh) connections to it.

{{< tip title="Tip - Use YugabyteDB Aeon" >}}
You can create a free cluster with [YugabyteDB Aeon](/preview/quick-start-yugabytedb-managed/), and open two *cloud shell* connections to it. These cloud shell connections open in two different browser tabs, which you can use to do the steps that follow.

{{< /tip >}}

### Transaction priority between concurrent operations

Consider an example scenario of an maintaining a bank account. Create the accounts table and insert rows into it, as follows:

```sql
create table account
  (
    name text not null,
    type text not null,
    balance money not null default '0.00'::money,
    primary key (name, type)
  );
insert into account values
  ('kevin','saving', 500),
  ('kevin','checking', 500);
```

To set a transaction priority for concurrent transactions, perform a deposit and a withdrawal at the same time, and set a higher priority to deposit transactions. To simulate this, perform the two operations concurrently - a withdrawal in one session and a deposit in another session. The deposit transaction starts after the withdrawal has been initiated, but occurs before the withdrawal is completed from a separate session, as demonstrated in the following table:

<table style="margin:0 5px;">
  <tr>
   <td style="text-align:center;"><span style="font-size: 22px;">session #1 (withdrawal, low priority)</span></td>
   <td style="text-align:center; border-left:1px solid rgba(158,159,165,0.5);"><span style="font-size: 22px;">session #2 (deposit, high priority)</span></td>
  </tr>

  <tr>
    <td style="width:50%;">
    Set the transaction priority to a lower range.
    <pre><code style="padding: 0 10px;">
set yb_transaction_priority_lower_bound = 0.4;
set yb_transaction_priority_upper_bound= 0.6;
    </code></pre>
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    Set the transaction priority to a higher range.
    <pre><code style="padding: 0 10px;">
set yb_transaction_priority_lower_bound = 0.7;
set yb_transaction_priority_upper_bound= 0.9;
    </code></pre>
    </td>
  </tr>

  <tr>
    <td style="width:50%;">
    Initiate the withdrawal of $100.
    <pre><code style="padding: 0 10px;">
begin transaction /*lower priority transaction*/;
update account set balance = balance - 100::money
    where name='kevin' and type='checking';
    </code></pre>
    The transaction has started, though not committed yet.
    <pre><code style="padding: 0 10px;">
select * from account;
 name  |   type   | balance
-------+----------+---------
 kevin | checking | $400.00
 kevin | saving   | $500.00
(2 rows)
    </code></pre>
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    </td>
  </tr>

  <tr>
    <td style="width:50%; border-right:1px solid rgba(158,159,165,0.5);">
    </td>
    <td style="width:50%;">
    Next, initiate the deposit of $200, which should have higher priority.
    <pre><code style="padding: 0 10px;">
begin transaction /*high priority transaction*/;
update account set balance = balance + 200::money
    where name='kevin' and type='checking';
    </code></pre>
    The transaction has started, though not committed yet.
    <pre><code style="padding: 0 10px;">
select * from account;
 name  |   type   | balance
-------+----------+---------
 kevin | checking | $700.00
 kevin | saving   | $500.00
(2 rows)
    </code></pre>
    </td>
  </tr>

  <tr>
    <td style="width:50%;">
    The withdrawal transaction will now abort because it conflicts with the higher priority deposit transaction.
    <pre><code style="padding: 0 10px;">
select * from account;
ERROR:  Operation failed. Try again: Unknown transaction,
        could be recently aborted: XXXX
    </code></pre>
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    </td>
  </tr>

  <tr>
    <td style="width:50%; border-right:1px solid rgba(158,159,165,0.5);">
    </td>
    <td style="width:50%;">
    The deposit transaction can now commit.
    <pre><code style="padding: 0 10px;">
commit;
COMMIT
yugabyte=> select * from account;
 name  |   type   | balance
-------+----------+---------
 kevin | checking | $700.00
 kevin | saving   | $500.00
(2 rows)
    </code></pre>
    </td>
  </tr>

</table>

### Show transaction priority types

The `yb_get_current_transaction_priority` function shows the transaction priority of the current transaction and the priority bucket the given priority belongs in. Transaction priority buckets are explained in detail in [Transaction Priorities](../../../architecture/transactions/transaction-priorities/). The following example demonstrates the usage of `yb_get_current_transaction_priority`.

1. From an active [ysqlsh](../../../admin/ysqlsh/#starting-ysqlsh) shell, create a table as follows:

    ```sql
    CREATE TABLE test_scan (i int, j int);
    ```

1. Start by setting the lower and upper bound values for your transaction.

    ```sql
    set yb_transaction_priority_lower_bound = 0.4;
    set yb_transaction_priority_upper_bound = 0.6;
    ```

1. In a transaction block, perform an insert and view the transaction priority as follows:

    ```sql
    BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
    INSERT INTO test_scan (i, j) values (1, 1), (2, 2), (3, 3);
    SELECT yb_get_current_transaction_priority();
    COMMIT;
    ```

    ```output
        yb_get_current_transaction_priority
    -------------------------------------------
      0.537144608 (Normal priority transaction)
    (1 row)
    ```

1. In the next transaction block, perform a `SELECT ... FOR UPDATE`, which results in a high priority transaction.

    ```sql
    set yb_transaction_priority_lower_bound = 0.1;
    set yb_transaction_priority_lower_bound = 0.4;
    BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
    SELECT i, j FROM test_scan WHERE i = 1 FOR UPDATE;
    SELECT yb_get_current_transaction_priority();
    COMMIT;
    ```

    ```output
        yb_get_current_transaction_priority
    -------------------------------------------
     0.212004009 (High priority transaction)
    (1 row)
    ```

   The transaction priority is randomly chosen between the lower and upper bound.

1. In the final transaction block, set `yb_transaction_priority_upper_bound` and `yb_transaction_priority_lower_bound` to be 1, and perform the same `SELECT ... FOR UPDATE` query as the previous one. This transaction type is of the highest priority.

    ```sql
    set yb_transaction_priority_upper_bound = 1;
    set yb_transaction_priority_lower_bound = 1;
    BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
    SELECT i, j FROM test_scan WHERE i = 1 FOR UPDATE;
    SELECT yb_get_current_transaction_priority();
    COMMIT;
    ```

    ```output
    yb_get_current_transaction_priority
    -------------------------------------
    Highest priority transaction
    (1 row)
    ```

For more information, see [Transaction priorities](../../../architecture/transactions/transaction-priorities/).

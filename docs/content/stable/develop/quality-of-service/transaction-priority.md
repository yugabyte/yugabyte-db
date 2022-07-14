---
title: Transaction priorities
headerTitle: Transaction priorities
linkTitle: Transaction priorities
description: Transaction priorities in YugabyteDB.
headcontent: Transaction priorities in YugabyteDB.
image: <div class="icon"><i class="fas fa-file-invoice-dollar"></i></div>
menu:
  stable:
    name: Transaction priorities
    identifier: develop-quality-of-service-transaction-priorities
    parent: develop-quality-of-service
    weight: 235
type: docs
---

YugabyteDB allows external applications to set the priority of individual transactions. When using optimistic concurrency control, it is possible to ensure that a *higher priority* transaction gets priority over a lower priority transaction. In this scenario, if these transactions conflict, the *lower priority* transaction is aborted. This behavior can be achieved by setting the pair of session variables `yb_transaction_priority_lower_bound` and `yb_transaction_priority_upper_bound`. A random number between the lower and upper bound is computed and assigned as the transaction priority for the transactions in that session. If this transaction conflicts with another, the value of transaction priority is compared with that of the conflicting transaction. The transaction with a higher priority value wins.

| Flag | Valid Range | Description |
| --- | --- | --- |
| `yb_transaction_priority_lower_bound` | Any value between 0 and 1, lower than the upper bound | Minimum transaction priority for transactions run in this session |
| `yb_transaction_priority_upper_bound` | Any value between 0 and 1, higher than the lower bound | Maximum transaction priority for transactions run in this session |


{{< note title="Note" >}}
Currently, transaction priorities work in the following scenarios:
* Works with YSQL only, not supported for YCQL
* Can be used only with optimistic concurrency control, not yet implemented for pessimistic concurrency control
* Only conflict resolution is prioritized, not resource consumption as a part

Some of the improvements are planned.

{{< /note >}}

It is possible to set the priority of a transaction using the two session variables `yb_transaction_priority_lower_bound` and `yb_transaction_priority_upper_bound` each of which can be set to a value between 0.0 and 1.0, as shown below. When a transaction is executed, a random priority between the lower and upper bound is assigned to it. This can be used as shown in the example below.

Let's create a YugabyteDB cluster, and open two separate `ysqlsh` connections to it.

{{< tip title="Tip - use YugabyteDB Managed" >}}
You can create a cluster in the free tier of YugabyteDB Managed, and open two *cloud shell* connections to it. These cloud shell connections open up in two different browser tabs, which can be used to do the steps below.

{{< /tip >}}

Let's create an example scenario of an accounts table, and insert a row into it as shown below.
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

Now, we're going to perform a deposit and a withdrawal at the same time. Also, assume we want to give higher priority to deposit transactions (when compared to withdrawals). To simulate this, we're going to perform two operations concurrently - a withdrawal in one session, and a deposit from a separate session. The deposit transaction starts after the withdrawal is initiated, but occurs before the withdrawal is completed from a separate session. This is shown below.


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
begin transaction /* lower priority transaction */;
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
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    </td>
    <td style="width:50%;">
    Next, initiate the deposit of $200, which should have higher priority.
    <pre><code style="padding: 0 10px;">
begin transaction /* high priority transaction */;
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
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
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

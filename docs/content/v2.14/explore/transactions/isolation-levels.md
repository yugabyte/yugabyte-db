---
title: Isolation Levels
headerTitle: Isolation Levels
linkTitle: Isolation Levels
description: Isolation Levels in YugabyteDB.
headcontent: Isolation Levels in YugabyteDB.
image: <div class="icon"><i class="fa-solid fa-file-invoice-dollar"></i></div>
menu:
  v2.14:
    name: Isolation Levels
    identifier: explore-transactions-isolation-levels-1-ysql
    parent: explore-transactions
    weight: 235
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../isolation-levels/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
<!--
  <li >
    <a href="../isolation-levels-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
-->
</ul>

Yugabyte supports three isolation levels in the transactional layer - Serializable, Snapshot, and Read Committed<sup>$</sup>. PostgreSQL (and the SQL standard) have four isolation levels - Serializable, Repeatable read, Read Committed, and Read uncommitted. The mapping between the PostgreSQL isolation levels in YSQL, along with which transaction anomalies can occur at each isolation level are shown below.

PostgreSQL Isolation  | YugabyteDB Equivalent | Dirty Read | Nonrepeatable Read | Phantom Read | Serialization Anomaly
-----------------|--------------|------------------------|---|---|---
Read <br/>uncommitted | Read Committed<sup>$</sup> | Allowed, but not in YSQL |  Possible | Possible | Possible
Read <br/>committed   | Read Committed<sup>$</sup> | Not possible | Possible | Possible | Possible
Repeatable <br/>read  | Snapshot | Not possible | Not possible | Allowed, but not in YSQL | Possible
Serializable     | Serializable | Not possible | Not possible | Not possible | Not possible

<sup>$</sup> Read Committed support is currently in [Tech Preview](/preview/releases/versioning/#feature-availability). Read Committed Isolation is supported only if the YB-TServer gflag `yb_enable_read_committed_isolation` is set to `true`. By default this gflag is `false` and in this case the Read Committed isolation level of the YugabyteDB transactional layer falls back to the stricter Snapshot Isolation (in which case `READ COMMITTED` and `READ UNCOMMITTED` of YSQL also in turn use Snapshot Isolation).

{{< note title="Note" >}}
The default isolation level for the YSQL API is essentially Snapshot (that is, the same as PostgreSQL's `REPEATABLE READ`) because `READ COMMITTED`, which is the YSQL APIs (and also PostgreSQL) syntactic default, maps to Snapshot Isolation (unless the YB-TServer gflag `yb_enable_read_committed_isolation` is set to `true`).

To set the transaction isolation level of a transaction, use the command `SET TRANSACTION`.

{{< /note >}}

As seen from the table above, the most strict isolation level is `Serializable`, which requires that any concurrent execution of a set of `Serializable` transactions is guaranteed to produce the same effect as running them in some serial (one transaction at a time) order. The other levels are defined by which anomalies must not occur as a result of interactions between concurrent transactions. Due to the definition of Serializable isolation, none of these anomalies are possible at that level. For reference, the various transaction anomalies are described briefly below:

* `Dirty read`: A transaction reads data written by a concurrent uncommitted transaction.

* `Nonrepeatable read`: A transaction re-reads data it has previously read and finds that data has been modified by another transaction (that committed since the initial read).

* `Phantom read`: A transaction re-executes a query returning a set of rows that satisfy a search condition and finds that the set of rows satisfying the condition has changed due to another recently-committed transaction.

* `Serialization anomaly`: The result of successfully committing a group of transactions is inconsistent with all possible orderings of running those transactions one at a time.

Let us now look at how Serializable, Snapshot and Read Committed isolation works in YSQL.

## Serializable Isolation

The *Serializable* isolation level provides the strictest transaction isolation. This level emulates serial transaction execution for all committed transactions; as if transactions had been executed one after another, serially, rather than concurrently. Serializable isolation can detect read-write conflicts in addition to write-write conflicts. This is accomplished by writing *provisional records* for read operations as well.

Let's use a bank overdraft protection example to illustrate this case. The hypothetical case is that there is a bank which allows depositors to withdraw money up to the total of what they have in all accounts. The bank will later automatically transfer funds as needed to close the day with a positive balance in each account. Within a single transaction they check that the total of all accounts exceeds the amount requested.

Let's say someone tries to withdraw $900 from two of their accounts simultaneously, each with $500 balances. At the `REPEATABLE READ` transaction isolation level, that could work; but if the `SERIALIZABLE` transaction isolation level is used, a read/write conflict will be detected and one of the transactions will be rejected.

The example can be set up with these statements:

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

<table style="margin:0 5px;">
  <tr>
   <td style="text-align:center;"><span style="font-size: 22px;">session #1</span></td>
   <td style="text-align:center; border-left:1px solid rgba(158,159,165,0.5);"><span style="font-size: 22px;">session #2</span></td>
  </tr>

  <tr>
    <td style="width:50%;">
    Begin a transaction in session #1 with the Serializable isolation level. The account total is $1000, so a $900 withdrawal is OK.
    <pre><code style="padding: 0 10px;">
begin isolation level serializable;<br/>
select type, balance from account
  where name = 'kevin';<br/>
   type   | balance
----------+---------
 saving   | $500.00
 checking | $500.00
(2 rows)
    </code></pre>
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    </td>
  </tr>

  <tr>
    <td style="width:50%;">
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    Begin a transaction in session #2 with the Serializable isolation level as well. Once again, the account total is $1000, so a $900 withdrawal is OK.
    <pre><code style="padding: 0 10px;">
begin isolation level serializable;<br/>
select type, balance from account
  where name = 'kevin';<br/>
   type   | balance
----------+---------
 saving   | $500.00
 checking | $500.00
(2 rows)
    </code></pre>
    </td>
  </tr>

  <tr>
    <td style="width:50%;">
    Withdraw $900 from the savings account, given the total is $1000 this should be OK.
    <pre><code style="padding: 0 10px;">
update account
  set balance = balance - 900::money
  where name = 'kevin' and type = 'saving';
    </code></pre>
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    </td>
  </tr>

  <tr>
    <td style="width:50%;">
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    Simultaneously, withdrawing $900 from the checking account is going to be a problem. This cannot co-exist with the other transaction's activity. This transaction would fail immediately.
    <pre><code style="padding: 0 10px;">
update account
  set balance = balance - 900::money
  where name = 'kevin' and type = 'checking';

ERROR:  40001: Operation failed.
  Try again.: Transaction aborted: XXXX
    </code></pre>
    </td>
  </tr>

  <tr>
    <td style="width:50%;">
    This transaction can now be committed.
    <pre><code style="padding: 0 10px;">
commit;<br/>
select type, balance from account
  where name = 'kevin';<br/>
   type   | balance
----------+----------
 checking |  $500.00
 saving   | -$400.00
(2 rows)
    </code></pre>
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    </td>
  </tr>

</table>

## Snapshot Isolation

The Snapshot isolation level only sees data committed before the transaction began (or in other words, it works on a "snapshot" of the table). Transactions running under Snapshot isolation do not see either uncommitted data or changes committed during transaction execution by other concurrently running transactions. Note that the query does see the effects of previous updates executed within its own transaction, even though they are not yet committed. This is a stronger guarantee than is required by the SQL standard for the `REPEATABLE READ` isolation level.

Snapshot isolation detects only write-write conflicts, it does not detect read-write conflicts. In other words:

* `INSERT`, `UPDATE`, and `DELETE` commands behave the same as SELECT in terms of searching for target rows. They will only find target rows that were committed as of the transaction start time.
* If such a target row might have already been updated (or deleted or locked) by another concurrent transaction by the time it is found. This scenario is called a *transaction conflict*, where the current transaction conflicts with the transaction that made (or is attempting to make) an update. In such cases, one of the two transactions get aborted, depending on priority.

{{< note title="Note" >}}
Applications using this level must be prepared to retry transactions due to serialization failures.
{{< /note >}}

Let's run through the scenario below to understand how transactions behave under the snapshot isolation level (which PostgreSQL's *Repeatable Read* maps to).

First, create an example table with sample data.

```sql
CREATE TABLE IF NOT EXISTS example (k INT PRIMARY KEY);
TRUNCATE TABLE example;
```

Next, connect to the cluster using two independent `ysqlsh` instances called *session #1* and *session #2* below.

{{< note title="Note" >}}
You can connect the session #1 and session #2 `ysqlsh` instances to the same server, or to different servers.
{{< /note >}}

<table style="margin:0 5px;">
  <tr>
   <td style="text-align:center;"><span style="font-size: 22px;">session #1</span></td>
   <td style="text-align:center; border-left:1px solid rgba(158,159,165,0.5);"><span style="font-size: 22px;">session #2</span></td>
  </tr>

  <tr>
    <td style="width:50%;">
    Begin a transaction in session #1. This will be snapshot isolation by default, meaning it will work against a snapshot of the database as of this point.
    <pre><code style="padding: 0 10px;">
BEGIN TRANSACTION;
    </code></pre>
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    </td>
  </tr>

  <tr>
    <td style="width:50%;">
    Insert a row, but let's not commit the transaction. This row should be visible only to this transaction.
    <pre><code style="padding: 0 10px;">
INSERT INTO example VALUES (1);<br/>
SELECT * FROM example;
 k
---
 1
(1 row)
    </code></pre>
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    </td>
  </tr>

  <tr>
    <td style="width:50%;">
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    Insert a different row here. Verify that the row inserted in the transaction in session #1 is not visible in this session.
    <pre><code style="padding: 0 10px;">
INSERT INTO example VALUES (2);<br/>
SELECT * FROM example;
 k
---
 2
(1 row)
    </code></pre>
    </td>
  </tr>

  <tr>
    <td style="width:50%;">
    The row inserted in the other session would not be visible here, because we're working against an older snapshot of the database. Let's verify that.
    <pre><code style="padding: 0 10px;">
SELECT * FROM example;
 k
---
 1
(1 row)
    </code></pre>
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    </td>
  </tr>

  <tr>
    <td style="width:50%;">
    Now let's commit this transaction. As long as the row(s) we're writing as a part of this transaction are not modified during the lifetime of the transaction, there would be no conflicts. Let's verify we can see all rows after the commit.
    <pre><code style="padding: 0 10px;">
COMMIT; <br/>
SELECT * FROM example;
 k
---
 1
 2
(2 rows)
    </code></pre>
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    </td>
  </tr>

</table>

## Read Committed Isolation

Read Committed support is currently in [Tech Preview](/preview/releases/versioning/#feature-availability).

This is same as Snapshot Isolation except that every statement in the transaction will see all data that has been committed before it is issued (note that this implicitly also means that the statement will see a consistent snapshot). In other words, each statement works on a new "snapshot" of the database that includes everything that is committed before the statement is issued. Conflict detection is the same as in Snapshot Isolation.

<table style="margin:0 5px;">
  <tr>
   <td style="text-align:center;"><span style="font-size: 22px;">session #1</span></td>
   <td style="text-align:center; border-left:1px solid rgba(158,159,165,0.5);"><span style="font-size: 22px;">session #2</span></td>
  </tr>

  <tr>
    <td style="width:50%;">
    Create a sample table.
    <pre><code style="padding: 0 10px;">
CREATE TABLE test (k int PRIMARY KEY, v int);
INSERT INTO test VALUES (1, 2);
    </code></pre>
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    </td>
  </tr>

  <tr>
    <td style="width:50%;">
    By default, the YB-TServer gflag yb_enable_read_committed_isolation=false. In this case, Read Committed maps to Snapshot Isolation at the transactional layer. So, READ COMMITTED of YSQL API in turn maps to Snapshot Isolation.
    <pre><code style="padding: 0 10px;">BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;
SELECT * FROM test;<br/>
 k | v
---+---
 1 | 2
(1 row)</code></pre>
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    </td>
  </tr>

  <tr>
    <td style="width:50%;">
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    Insert a new row.
    <pre><code style="padding: 0 10px;">INSERT INTO test VALUES (2, 3);</code></pre>
    </td>
  </tr>

  <tr>
    <td style="width:50%;">
    Perform read again in the same transaction. Note that the recently inserted row (2, 3) isn't
    visible to the statement because Read Committed is disabled at the transactional layer and maps to
    Snapshot (in which the whole transaction sees a consistent snapshot of the database).
    <pre><code style="padding: 0 10px;">SELECT * FROM test;
COMMIT;<br/>
 k | v
---+---
 1 | 2
(1 row)</code></pre>
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    </td>
  </tr>

  <tr>
    <td style="width:50%;">
    Set the YB-TServer gflag yb_enable_read_committed_isolation=true
    <pre><code style="padding: 0 10px;">BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;
SELECT * FROM test;<br/>
 k | v
---+---
 1 | 2
 2 | 3
(2 rows)</code></pre>
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    </td>
  </tr>

  <tr>
    <td style="width:50%;">
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    In another session, insert a new row.
    <pre><code style="padding: 0 10px;">INSERT INTO test VALUES (3, 4);</code></pre>
    </td>
  </tr>

  <tr>
    <td style="width:50%;">
    Perform read again in the same transaction. This time, the statement will be able to see the
    row (3, 4) that was committed after this transaction was started but before the statement was issued.
    <pre><code style="padding: 0 10px;">
SELECT * FROM test;
 k | v
---+---
 1 | 2
 2 | 3
 3 | 4
(3 rows)
    </code></pre>
    </td>
    <td style="width:50%; border-left:1px solid rgba(158,159,165,0.5);">
    </td>
  </tr>

</table>

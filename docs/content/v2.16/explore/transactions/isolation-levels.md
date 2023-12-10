---
title: Isolation levels
headerTitle: Isolation levels
linkTitle: Isolation levels
description: Isolation Levels in YugabyteDB.
headcontent: Serializable, Snapshot, and Read committed isolation in YugabyteDB
menu:
  v2.16:
    name: Isolation levels
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

YugabyteDB supports three isolation levels in the transactional layer - Serializable, Snapshot, and Read committed. PostgreSQL (and the SQL standard) have four isolation levels - Serializable, Repeatable read, Read committed, and Read uncommitted. The mapping between the PostgreSQL isolation levels in YSQL, along with which transaction anomalies can occur at each isolation level, are shown in the following table.

| PostgreSQL Isolation | YugabyteDB Equivalent | Dirty Read | Non-repeatable Read | Phantom Read | Serialization Anomaly |
| :------------------- | :-------------------- | :--------- | :------------------ | :----------- | :-------------------- |
Read uncommitted | Read Committed<sup>$</sup> | Allowed, but not in YSQL |  Possible | Possible | Possible
Read committed   | Read Committed<sup>$</sup> | Not possible | Possible | Possible | Possible
Repeatable read  | Snapshot | Not possible | Not possible | Allowed, but not in YSQL | Possible
Serializable     | Serializable | Not possible | Not possible | Not possible | Not possible

<sup>$</sup> Read committed support is currently in [Tech Preview](/preview/releases/versioning/#feature-availability). Read committed Isolation is supported only if the YB-Tserver flag `yb_enable_read_committed_isolation` is set to `true`. By default this flag is `false` and in this case the Read committed isolation level of the YugabyteDB transactional layer falls back to the stricter Snapshot Isolation (in which case `READ COMMITTED` and `READ UNCOMMITTED` of YSQL also in turn use Snapshot Isolation).

{{< note title="Note" >}}
The default isolation level for the YSQL API is essentially Snapshot (that is, the same as PostgreSQL's `REPEATABLE READ`) because `READ COMMITTED`, which is the YSQL API's (and also PostgreSQL's) syntactic default, maps to Snapshot Isolation (unless the YB-Tserver flag `yb_enable_read_committed_isolation` is set to `true`).

To set the transaction isolation level of a transaction, use the command `SET TRANSACTION`.

{{< /note >}}

As seen from the preceding table, the most strict isolation level is `Serializable`, which requires that any concurrent execution of a set of `Serializable` transactions is guaranteed to produce the same effect as running them in some serial (one transaction at a time) order. The other levels are defined by which anomalies must not occur as a result of interactions between concurrent transactions. Due to the definition of Serializable isolation, none of these anomalies are possible at that level. For reference, the various transaction anomalies are as follows:

* **Dirty read**: A transaction reads data written by a concurrent uncommitted transaction.

* **Nonrepeatable read**: A transaction re-reads data it has previously read and finds that data has been modified by another transaction (that committed after the initial read).

* **Phantom read**: A transaction re-executes a query returning a set of rows that satisfy a search condition and finds that the set of rows satisfying the condition has changed due to another recently-committed transaction.

* **Serialization anomaly**: The result of successfully committing a group of transactions is inconsistent with all possible orderings of running those transactions one at a time.

## Serializable isolation

The *Serializable* isolation level provides the strictest transaction isolation. This level emulates serial transaction execution for all committed transactions; as if transactions had been executed one after another, serially, rather than concurrently. Serializable isolation can detect read-write conflicts in addition to write-write conflicts. This is accomplished by writing *provisional records* for read operations as well.

### Example

The following bank overdraft protection example illustrates this case. The hypothetical case is that there is a bank which allows depositors to withdraw money up to the total of what they have in all accounts. The bank will later automatically transfer funds as needed to close the day with a positive balance in each account. In a single transaction they check that the total of all accounts exceeds the amount requested.

{{% explore-setup-single %}}

Suppose someone tries to withdraw $900 from two of their accounts simultaneously, each with $500 balances. At the `REPEATABLE READ` transaction isolation level, that could work; but if the `SERIALIZABLE` transaction isolation level is used, a read/write conflict is detected and one of the transactions rejected.

Set up the example with the following statements:

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

Next, connect to the cluster using two independent ysqlsh instances, referred to as session #1 and session #2.

<table>
  <tr>
   <td style="text-align:center;">session #1</td>
   <td style="text-align:center;">session #2</td>
  </tr>

  <tr>
    <td>

Begin a transaction in session #1 with the Serializable isolation level. The account total is $1000, so a $900 withdrawal is OK.

```sql
begin isolation level serializable;
select type, balance from account
  where name = 'kevin';
```

```output
   type   | balance
----------+---------
 saving   | $500.00
 checking | $500.00
(2 rows)
```

  </td>
    <td>
    </td>
  </tr>

  <tr>
    <td>
    </td>
    <td>

Begin a transaction in session #2 with the Serializable isolation level as well. Once again, the account total is $1000, so a $900 withdrawal is OK.

```sql
begin isolation level serializable;
select type, balance from account
  where name = 'kevin';
```

```output
   type   | balance
----------+---------
 saving   | $500.00
 checking | $500.00
(2 rows)
```

  </td>
  </tr>

  <tr>
    <td>

Withdraw $900 from the savings account, given the total is $1000 this should be OK.

```sql
update account
  set balance = balance - 900::money
  where name = 'kevin' and type = 'saving';
```

  </td>
    <td>
    </td>
  </tr>

  <tr>
    <td></td>
    <td>

Simultaneously, withdrawing $900 from the checking account is going to be a problem. This can't co-exist with the other transaction's activity. This transaction would fail immediately.

```sql
update account
  set balance = balance - 900::money
  where name = 'kevin' and type = 'checking';
```

```output
ERROR:  40001: Operation failed.
  Try again.: Transaction aborted: XXXX
```

  </td>
  </tr>

  <tr>
    <td>

This transaction can now be committed.

```sql
commit;
select type, balance from account
  where name = 'kevin';
```

```output
   type   | balance
----------+----------
 checking |  $500.00
 saving   | -$400.00
(2 rows)
```

  </td>
    <td>
    </td>
  </tr>

</table>

## Snapshot isolation

The Snapshot isolation level only sees data committed before the transaction began (or in other words, it works on a "snapshot" of the table). Transactions running under Snapshot isolation do not see either uncommitted data or changes committed during transaction execution by other concurrently running transactions. Note that the query does see the effects of previous updates executed in its own transaction, even though they are not yet committed. This is a stronger guarantee than is required by the SQL standard for the `REPEATABLE READ` isolation level.

Snapshot isolation detects only write-write conflicts, it does not detect read-write conflicts. In other words:

* `INSERT`, `UPDATE`, and `DELETE` commands behave the same as SELECT in terms of searching for target rows. They will only find target rows that were committed as of the transaction start time.
* If such a target row might have already been updated (or deleted or locked) by another concurrent transaction by the time it is found. This scenario is called a *transaction conflict*, where the current transaction conflicts with the transaction that made (or is attempting to make) an update. In such cases, one of the two transactions get aborted, depending on priority.

{{< note title="Note" >}}
Applications using this level must be prepared to retry transactions due to serialization failures.
{{< /note >}}

### Example

The following scenario shows how transactions behave under the snapshot isolation level (which PostgreSQL's *Repeatable Read* maps to).

Create an example table with sample data as follows:

```sql
CREATE TABLE IF NOT EXISTS example (k INT PRIMARY KEY);
TRUNCATE TABLE example;
```

Next, connect to the cluster using two independent `ysqlsh` instances, referred to as *session #1* and *session #2*.

<table>
  <tr>
   <td style="text-align:center;">session #1</td>
   <td style="text-align:center;">session #2</td>
  </tr>

  <tr>
    <td>

Begin a transaction in session #1. This is snapshot isolation by default, meaning it will work against a snapshot of the database as of this point.

```sql
BEGIN TRANSACTION;
```

  </td>
    <td>
    </td>
  </tr>

  <tr>
    <td>

Insert a row, but don't commit the transaction. This row should be visible only to this transaction.

```sql
INSERT INTO example VALUES (1);
SELECT * FROM example;
```

```output
 k
---
 1
(1 row)
```

  </td>
    <td>
    </td>
  </tr>

  <tr>
    <td>
    </td>
    <td>

Insert a different row. Verify that the row inserted in the transaction in session #1 is not visible in this session as follows:

```sql
INSERT INTO example VALUES (2);
SELECT * FROM example;
```

```output
 k
---
 2
(1 row)
```

  </td>
  </tr>

  <tr>
    <td>

The row inserted in the other session is not visible here, because you're working against an older snapshot of the database. Verify that as follows:

```sql
SELECT * FROM example;
```

```output
 k
---
 1
(1 row)
```

  </td>
    <td>
    </td>
  </tr>

  <tr>
    <td>

Now commit this transaction. As long as the row(s) you're writing as a part of this transaction are not modified during the lifetime of the transaction, there would be no conflicts. Verify you can see all rows after the commit as follows:

```sql
COMMIT;
SELECT * FROM example;
```

```output
 k
---
 1
 2
(2 rows)
```

  </td>
    <td>
    </td>
  </tr>

</table>

## Read committed isolation

Read committed support is currently in [Tech Preview](/preview/releases/versioning/#feature-availability).

Read committed isolation is the same as Snapshot isolation except that every statement in the transaction will see all data that has been committed before it is issued (note that this implicitly also means that the statement will see a consistent snapshot). In other words, each statement works on a new "snapshot" of the database that includes everything that is committed before the statement is issued. Conflict detection is the same as in Snapshot isolation.

### Example

Create a sample table as follows:

```sql
CREATE TABLE test (k int PRIMARY KEY, v int);
INSERT INTO test VALUES (1, 2);
```

Next, connect to the cluster using two independent ysqlsh instances, referred to as session #1 and session #2.

<table>
  <tr>
   <td style="text-align:center;">session #1</td>
   <td style="text-align:center;">session #2</td>
  </tr>

  <tr>
    <td>

By default, the YB-TServer flag `yb_enable_read_committed_isolation` is false. In this case, Read committed maps to Snapshot isolation at the transactional layer. So, READ COMMITTED of YSQL API in turn maps to Snapshot Isolation.

```sql
BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;
SELECT * FROM test;
```

```output
 k | v
---+---
 1 | 2
(1 row)
```

  </td>
    <td>
    </td>
  </tr>

  <tr>
    <td>
    </td>
    <td>

Insert a new row.

```sql
INSERT INTO test VALUES (2, 3);
```

  </td>
  </tr>

  <tr>
    <td>

Perform the read again in the same transaction.

```sql
SELECT * FROM test;
COMMIT;
```

```output
 k | v
---+---
 1 | 2
(1 row)
```

The inserted row (2, 3) isn't visible because Read committed is disabled at the transactional layer and maps to Snapshot (in which the whole transaction sees a consistent snapshot of the database).

Set the YB-Tserver flag yb_enable_read_committed_isolation=true.

```sql
BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;
SELECT * FROM test;
```

```output
 k | v
---+---
 1 | 2
 2 | 3
(2 rows)
```

  </td>
    <td>
    </td>
  </tr>

  <tr>
    <td>
    </td>
    <td>

Insert a new row.

```sql
INSERT INTO test VALUES (3, 4);
```

  </td>
  </tr>

  <tr>
    <td>

Perform the read again in the same transaction.

```sql
SELECT * FROM test;
```

```output
 k | v
---+---
 1 | 2
 2 | 3
 3 | 4
(3 rows)
```

This time, the statement can see the row (3, 4) that was committed after this transaction was started but before the statement was issued.

  </td>
    <td>
    </td>
  </tr>

</table>

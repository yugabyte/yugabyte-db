---
title: Isolation levels
headerTitle: Isolation levels
linkTitle: Isolation levels
description: Isolation Levels in YugabyteDB.
headcontent: Serializable, Snapshot, and Read committed isolation in YugabyteDB
image: <div class="icon"><i class="fa-solid fa-file-invoice-dollar"></i></div>
menu:
  preview:
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

YugabyteDB supports three isolation levels in the transactional layer:

- Serializable
- Snapshot
- Read committed {{<badge/tp>}}

The default isolation level for the YSQL API is essentially Snapshot (that is, the same as PostgreSQL's `REPEATABLE READ`) because, by default, Read committed, which is the YSQL API and PostgreSQL _syntactic_ default, maps to Snapshot isolation.

To enable Read committed (currently in [Tech Preview](/preview/releases/versioning/#feature-availability)), you must set the YB-TServer flag `yb_enable_read_committed_isolation` to `true`. By default this flag is `false` and the Read committed isolation level of the YugabyteDB transactional layer falls back to the stricter Snapshot isolation (in which case `READ COMMITTED` and `READ UNCOMMITTED` of YSQL also in turn use Snapshot isolation).

{{< tip title="Tip" >}}

To avoid serializable errors, that is, to run applications with no retry logic, keep the default `READ COMMITTED` isolation (`--ysql_default_transaction_isolation`), and set the YB-TServer `--yb_enable_read_committed_isolation` flag to true. This enables read committed isolation.

{{< /tip >}}

To set the transaction isolation level of a transaction, use the command `SET TRANSACTION`.

## Isolation levels in PostgreSQL and YSQL

PostgreSQL (and the SQL standard) has four isolation levels: Serializable, Repeatable read, Read committed, and Read uncommitted.

The following table shows the mapping between the PostgreSQL isolation levels in YSQL, along with transaction anomalies that can occur at each isolation level:

| PostgreSQL Isolation | YugabyteDB Equivalent     | Dirty Read | Non-repeatable Read | Phantom Read | Serialization Anomaly |
| :------------------- | :------------------------ | :--------- | :------------------ | :----------- | :-------------------- |
| Read uncommitted | Read Committed {{<badge/tp>}} | Allowed, but not in YSQL |  Possible | Possible | Possible |
| Read committed   | Read Committed {{<badge/tp>}} | Not possible | Possible     | Possible | Possible |
| Repeatable read  | Snapshot                      | Not possible | Not possible | Allowed, but not in YSQL | Possible |
| Serializable     | Serializable                  | Not possible | Not possible | Not possible | Not possible |

The strictest isolation level is Serializable, which requires that any concurrent execution of a set of Serializable transactions is guaranteed to produce the same effect as running them in some serial order (one transaction at a time).

The other levels are defined by which anomalies (none of which are possible with serializable) must not occur as a result of interactions between concurrent transactions. Due to the definition of Serializable isolation, none of these anomalies are possible at that level.

The following are the various transaction anomalies:

- Dirty read: A transaction reads data written by a concurrent uncommitted transaction.

- Non-repeatable read: A transaction rereads data it has previously read and finds that data has been modified by another transaction committed after the initial read.

- Phantom read: A transaction re-executes a query returning a set of rows that satisfy a search condition and finds that the set of rows satisfying the condition has changed due to another recently-committed transaction.

- Serialization anomaly: The result of successfully committing a group of transactions is inconsistent with all possible orderings of running those transactions one at a time.

## Serializable isolation

{{<explore-setup-single>}}

The Serializable isolation level provides the strictest transaction isolation. This level emulates serial transaction execution for all committed transactions, as if transactions had been executed one after another, serially rather than concurrently. Serializable isolation can detect read-write conflicts in addition to write-write conflicts. This is accomplished by writing provisional records for read operations as well.

The Serializable isolation can be demonstrated by a bank overdraft protection example.

The hypothetical case is that there is a bank which allows depositors to withdraw money up to the total of what they have in all accounts. The bank later automatically transfers funds as needed to close the day with a positive balance in each account. In a single transaction, they check that the total of all accounts exceeds the amount requested.

Suppose someone tries to withdraw $900 from two of their accounts simultaneously, each with $500 balances. At the `REPEATABLE READ` transaction isolation level, that could work, but if the `SERIALIZABLE` transaction isolation level is used, a read-write conflict is detected and one of the transactions is rejected.

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

Next, connect to the universe using two independent `ysqlsh` instances, referred to as session #1 and session #2.

<table>
  <tr>
   <td style="text-align:center;">session #1</td>
   <td style="text-align:center;">session #2</td>
  </tr>

  <tr>
    <td>

Begin a transaction in session #1 with the Serializable isolation level. The account total is $1000, so a $900 withdrawal is fine.

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

Begin a transaction in session #2 also with the Serializable isolation level. Once again, the account total is $1000, so a $900 withdrawal is fine.

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

Withdraw $900 from the savings account, given the total is $1000 this should be fine.

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

However, simultaneously withdrawing $900 from the checking account is going to be a problem. This cannot co-exist with the other transaction's activity. This transaction would fail immediately.

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

The Snapshot isolation level is only aware of data committed before the transaction began (in other words, it works on a snapshot of the table). Transactions running under Snapshot isolation are not aware of either uncommitted data or changes committed during transaction execution by other concurrently running transactions. Note that the query is aware of the effects of previous updates executed in its own transaction, even though they are not yet committed. This is a stronger guarantee than is required by the SQL standard for the `REPEATABLE READ` isolation level.

Snapshot isolation detects only write-write conflicts; it does not detect read-write conflicts. That is:

- `INSERT`, `UPDATE`, and `DELETE` commands behave in the same way as `SELECT` in terms of searching for target rows. They can only find target rows that were committed as of the transaction start time.
- If such a target row might have already been updated, deleted, or locked by another concurrent transaction by the time it is found. This is called a transaction conflict, where the current transaction conflicts with the transaction that made or is attempting to make an update. In such cases, one of the two transactions is aborted, depending on priority.

Applications using this level must be prepared to retry transactions due to serialization failures.

Consider an example of transactions' behavior under the Snapshot isolation level (mapped to PostgreSQL's Repeatable Read level).

Create a table with sample data, as follows:

```sql
CREATE TABLE IF NOT EXISTS example (k INT PRIMARY KEY);
TRUNCATE TABLE example;
```

Next, connect to the universe using two independent `ysqlsh` instances, referred to as session #1 and session #2:

<table>
  <tr>
   <td style="text-align:center;">session #1</td>
   <td style="text-align:center;">session #2</td>
  </tr>

  <tr>
    <td>

Begin a transaction in session #1. This is Snapshot isolation by default, meaning it will work against a snapshot of the database as of this point:

```sql
BEGIN TRANSACTION;
```

  </td>
    <td>
    </td>
  </tr>

  <tr>
    <td>

Insert a row, but do not commit the transaction. This row should be visible only to this transaction:

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

Insert a different row. Verify that the row inserted in the transaction in session #1 is not visible in this session, as follows:

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

The row inserted in the other session is not visible here because you are working against an older snapshot of the database. Verify that, as follows:

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

Commit this transaction. As long as the rows you are writing as a part of this transaction are not modified during the lifetime of the transaction, there would be no conflicts. Verify that all rows are visible after the commit, as follows:

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

{{< note >}}

Read Committed is [Tech Preview](/preview/releases/versioning/#feature-availability).

{{</note >}}

Read committed isolation is the same as Snapshot isolation, except that every statement in the transaction is aware of all data that has been committed before it has been issued (this implicitly means that the statement will see a consistent snapshot). In other words, each statement works on a new snapshot of the database that includes everything that has been committed before the statement is issued. Conflict detection is the same as in Snapshot isolation.

Consider an example of transactions' behavior under the Read committed isolation level.

Create a table, as follows:

```sql
CREATE TABLE test (k int PRIMARY KEY, v int);
INSERT INTO test VALUES (1, 2);
```

Connect to the universe using two independent `ysqlsh` instances, referred to as session #1 and session #2:

<table>
  <tr>
   <td style="text-align:center;">session #1</td>
   <td style="text-align:center;">session #2</td>
  </tr>

  <tr>
    <td>

By default, the YB-TServer flag `yb_enable_read_committed_isolation` is false. In this case, Read committed maps to Snapshot isolation at the transactional layer. So, `READ COMMITTED` of YSQL API in turn maps to Snapshot Isolation:

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

Insert a new row, as follows:

```sql
INSERT INTO test VALUES (2, 3);
```

  </td>
  </tr>

  <tr>
    <td>

Perform the read again in the same transaction, as follows:

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

The inserted row (2, 3) is not visible because Read committed is disabled at the transactional layer and maps to Snapshot in which the whole transaction sees a consistent snapshot of the database.

Set the YB-Tserver flag `yb_enable_read_committed_isolation` to `true`:

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

Insert a new row, as follows:

```sql
INSERT INTO test VALUES (3, 4);
```

  </td>
  </tr>

  <tr>
    <td>

Perform the read again in the same transaction, as follows:

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

This time, the statement can see the row (3, 4) that was committed after this transaction had been started but before the statement has been issued.

  </td>
    <td>
    </td>
  </tr>

</table>

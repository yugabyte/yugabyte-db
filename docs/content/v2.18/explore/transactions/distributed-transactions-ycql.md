---
title: Distributed transactions in YCQL
headerTitle: Distributed transactions
linkTitle: Distributed transactions
description: Understand distributed transactions in YugabyteDB using YCQL.
headcontent:
menu:
  v2.18:
    name: Distributed transactions
    identifier: explore-transactions-distributed-transactions-2-ycql
    parent: explore-transactions
    weight: 230
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../distributed-transactions-ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../distributed-transactions-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

The best way to understand distributed transactions in YugabyteDB is through examples.

To learn about how YugabyteDB handles failures during transactions, see [High availability of transactions](../../fault-tolerance/transaction-availability/).

{{% explore-setup-single %}}

## Create a table

Create a keyspace, as follows:

```sql
ycqlsh> CREATE KEYSPACE banking;
```

The YCQL table should be created with the `transactions` property enabled. The statement should be similar to the following:

```sql
ycqlsh> CREATE TABLE banking.accounts (
  account_name varchar,
  account_type varchar,
  balance float,
  PRIMARY KEY ((account_name), account_type)
) with transactions = { 'enabled' : true };
```

You can verify that this table has transactions enabled by running the following query:

```sql
ycqlsh> select keyspace_name, table_name, transactions from system_schema.tables
where keyspace_name='banking' AND table_name = 'accounts';
```

```output
 keyspace_name | table_name | transactions
---------------+------------+---------------------
       banking |   accounts | {'enabled': 'true'}

(1 rows)
```

## Insert sample data

Populate the table with sample data, as follows:

```sql
INSERT INTO banking.accounts (account_name, account_type, balance) VALUES ('John', 'savings', 1000);
INSERT INTO banking.accounts (account_name, account_type, balance) VALUES ('John', 'checking', 100);
INSERT INTO banking.accounts (account_name, account_type, balance) VALUES ('Smith', 'savings', 2000);
INSERT INTO banking.accounts (account_name, account_type, balance) VALUES ('Smith', 'checking', 50);
```

Execute the following statement to retrieve the balances for John and Smith:

```sql
ycqlsh> select * from banking.accounts;
```

```output
 account_name | account_type | balance
--------------+--------------+---------
         John |     checking |     100
         John |      savings |    1000
        Smith |     checking |      50
        Smith |      savings |    2000
```

Check John's balance, as follows:

```sql
ycqlsh> SELECT SUM(balance) as Johns_balance FROM banking.accounts WHERE account_name='John';
```

```output
 johns_balance
---------------
          1100
```

Check Smith's balance, as follows:

```sql
ycqlsh> SELECT SUM(balance) as smiths_balance FROM banking.accounts WHERE account_name='Smith';
```

```output
 smiths_balance
----------------
           2050

```

## Execute a transaction

Suppose John transfers $200 from his savings account to his checking account. This has to be a transactional operation, as per the following:

```sql
BEGIN TRANSACTION
  UPDATE banking.accounts SET balance = balance - 200 WHERE account_name='John' AND account_type='savings';
  UPDATE banking.accounts SET balance = balance + 200 WHERE account_name='John' AND account_type='checking';
END TRANSACTION;
```

Execute the following statement to select the value of John's account:

```sql
ycqlsh> select * from banking.accounts where account_name='John';
```
```output
 account_name | account_type | balance
--------------+--------------+---------
         John |     checking |     300
         John |      savings |     800
```

Execute the following statement to check John's balance:

```sql
ycqlsh> SELECT SUM(balance) as Johns_balance FROM banking.accounts WHERE account_name='John';
```
The following output demonstrates that the total balance is the same $1100 as before:

```output
 johns_balance
---------------
          1100
```

Further, the checking and savings account balances for John should have been written at the same write timestamp, as per the following:

```sql
ycqlsh> select account_name, account_type, balance, writetime(balance)
from banking.accounts where account_name='John';
```

```output
 account_name | account_type | balance | writetime(balance)
--------------+--------------+---------+--------------------
         John |     checking |     300 |   1517898028890171
         John |      savings |     800 |   1517898028890171
```

Now suppose John transfers the $200 from his checking account to Smith's checking account. Run the following transaction:

```sql
BEGIN TRANSACTION
  UPDATE banking.accounts SET balance = balance - 200 WHERE account_name='John' AND account_type='checking';
  UPDATE banking.accounts SET balance = balance + 200 WHERE account_name='Smith' AND account_type='checking';
END TRANSACTION;
```

## Verify

Execute the following query to verify that the transfer was made as intended and that the time at which the two accounts were updated is identical:

```sql
ycqlsh> select account_name, account_type, balance, writetime(balance) from banking.accounts;
```

```output
 account_name | account_type | balance | writetime(balance)
--------------+--------------+---------+--------------------
         John |     checking |     100 |   1517898167629366
         John |      savings |     800 |   1517898028890171
        Smith |     checking |     250 |   1517898167629366
        Smith |      savings |    2000 |   1517894361290020
```

Execute the following query to verify that the net balance for John has decreased by $200 which that of Smith has increased by $200:

```sql
ycqlsh> SELECT SUM(balance) as Johns_balance FROM banking.accounts WHERE account_name='John';
```

```output
 johns_balance
---------------
           900
```

Check Smith's balance, as follows:

```sql
ycqlsh> SELECT SUM(balance) as smiths_balance FROM banking.accounts WHERE account_name='Smith';
```

```output
 smiths_balance
----------------
           2250
```

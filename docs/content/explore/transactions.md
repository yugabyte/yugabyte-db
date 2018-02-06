---
title: Transactions
weight: 270
---

**NOTE: This feature is currently in beta.**

Transactions batch a multiple steps operation into a single, all-or-nothing operation. The intermediate states of the database between the steps in a transaction are not visible to other concurrent transactions or the end user. If the transaction encounters any failures that prevents it from completing successfully, none of the steps are applied to the database.

YugaByte DB is designed to support transactions at the following isolation levels:

- `Serializable` (currently supported)
- `Snapshot Isolation` (work in progress)

You can [read more about transactions](/architecture/concepts/transactions/) in our architecture docs.


## 1. Pre-requisites

- Follow these instructions to [install YugaByte DB](/quick-start/install/)

- Follow these instructions to [create a local cluster](/create-local-cluster/)

## 2. Creating a table for transactions

Connect to the cluster using `cqlsh`.

```sh
$ ./bin/cqlsh
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh>
```

Create a keyspace.

```sh
cqlsh> CREATE KEYSPACE banking;
```

Create a table with the `transactions` property set to enabled.

```sh
CREATE TABLE banking.accounts (
  account_name varchar,
  account_type varchar,
  balance float,
  PRIMARY KEY ((account_name), account_type)
) with transactions = { 'enabled' : true };
```

You can verify that this table has transactions enabled on it by querying the 

```sh
cqlsh> select keyspace_name, table_name, transactions from system_schema.tables
where keyspace_name='banking' AND table_name = 'accounts';

 keyspace_name | table_name | transactions
---------------+------------+---------------------
       banking |   accounts | {'enabled': 'true'}

(1 rows)
```


## 3. Write some sample data

Let us seed this table with some sample data.

```sh
INSERT INTO banking.accounts (account_name, account_type, balance) VALUES ('John', 'savings', 1000);
INSERT INTO banking.accounts (account_name, account_type, balance) VALUES ('John', 'checking', 100);
INSERT INTO banking.accounts (account_name, account_type, balance) VALUES ('Smith', 'savings', 2000);
INSERT INTO banking.accounts (account_name, account_type, balance) VALUES ('Smith', 'checking', 50);
```

Here are the balances for John and Smith.

```sh
cqlsh> select * from banking.accounts;

 account_name | account_type | balance
--------------+--------------+---------
         John |     checking |     100
         John |      savings |    1000
        Smith |     checking |      50
        Smith |      savings |    2000
```

```sh
cqlsh> SELECT SUM(balance) as Johns_balance FROM banking.accounts WHERE account_name='John';

 johns_balance
---------------
          1100
```

```sh
cqlsh> SELECT SUM(balance) as smiths_balance FROM banking.accounts WHERE account_name='Smith';

 smiths_balance
----------------
           2050

```


## 4. Execute some transactions

Here are a couple of examples of executing transactions.

- Let us say John transfers $200 from his savings account to his checking account. This has to be a transactional operation. This can be achieved as follows.

```sh
BEGIN TRANSACTION;
UPDATE banking.accounts SET balance = balance - 200 WHERE account_name='John' AND account_type='savings';
UPDATE banking.accounts SET balance = balance + 200 WHERE account_name='John' AND account_type='checking';
END TRANSACTION;
```

If we now selected the value of John's account, we should see the amounts reflected. The total balance should be the same $1100 as before.

```sh
cqlsh> select * from banking.accounts where account_name='John';

 account_name | account_type | balance
--------------+--------------+---------
         John |     checking |     300
         John |      savings |     800
```

```sh
cqlsh> SELECT SUM(balance) as Johns_balance FROM banking.accounts WHERE account_name='John';

 johns_balance
---------------
          1100
```


Further, the checking and savings account balances for John should have been written at the same write timestamp.

```sh
cqlsh> select account_name, account_type, balance, writetime(balance) 
from banking.accounts where account_name='John';

 account_name | account_type | balance | writetime(balance)
--------------+--------------+---------+--------------------
         John |     checking |     300 |   1517898028890171
         John |      savings |     800 |   1517898028890171
```


- Now let us say John transfers the $200 from his checking account to Smith's checking account. We can accomplish that with the following transaction.

```sh
BEGIN TRANSACTION;
UPDATE banking.accounts SET balance = balance - 200 WHERE account_name='John' AND account_type='checking';
UPDATE banking.accounts SET balance = balance + 200 WHERE account_name='Smith' AND account_type='checking';
END TRANSACTION;
```

We can verify the transfer was made as we intended, and also verify that the time at which the two accounts were updated are identical by performing the following query.

```sh
cqlsh> select account_name, account_type, balance, writetime(balance) from banking.accounts;

 account_name | account_type | balance | writetime(balance)
--------------+--------------+---------+--------------------
         John |     checking |     100 |   1517898167629366
         John |      savings |     800 |   1517898028890171
        Smith |     checking |     250 |   1517898167629366
        Smith |      savings |    2000 |   1517894361290020
```

The net balance for John should have decreased by $200 which that of Smith should have increased by $200.

```sh
cqlsh> SELECT SUM(balance) as Johns_balance FROM banking.accounts WHERE account_name='John';

 johns_balance
---------------
           900
```

```sh
cqlsh> SELECT SUM(balance) as smiths_balance FROM banking.accounts WHERE account_name='Smith';

 smiths_balance
----------------
           2250
```


## 5. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```sh
$ ./bin/yb-ctl destroy
```


## 1. Setup - create universe

If you have a previously running local universe, destroy it using the following.
<div class='copy separator-dollar'>
```sh
$ ./bin/yb-ctl destroy
```
</div>

Start a new local cluster - by default, this will create a 3 node universe with a replication factor of 3.
<div class='copy separator-dollar'>
```sh
$ ./bin/yb-ctl create
```
</div>

## 2. Create a table for transactions

Connect to the cluster using `cqlsh`.
<div class='copy separator-dollar'>
```sh
$ ./bin/cqlsh
```
</div>
```sh
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh>
```

Create a keyspace.
<div class='copy separator-gt'>
```sql
cqlsh> CREATE KEYSPACE banking;
```
</div>

Create a table with the `transactions` property set to enabled.
<div class='copy separator-gt'>
```sql
cqlsh> CREATE TABLE banking.accounts (
  account_name varchar,
  account_type varchar,
  balance float,
  PRIMARY KEY ((account_name), account_type)
) with transactions = { 'enabled' : true };
```
</div>

You can verify that this table has transactions enabled on it by querying the 
<div class='copy separator-gt'>
```sql
cqlsh> select keyspace_name, table_name, transactions from system_schema.tables
where keyspace_name='banking' AND table_name = 'accounts';
```
</div>
```sql
 keyspace_name | table_name | transactions
---------------+------------+---------------------
       banking |   accounts | {'enabled': 'true'}

(1 rows)
```


## 3. Insert sample data

Let us seed this table with some sample data.

```{.sql .copy}
INSERT INTO banking.accounts (account_name, account_type, balance) VALUES ('John', 'savings', 1000);
INSERT INTO banking.accounts (account_name, account_type, balance) VALUES ('John', 'checking', 100);
INSERT INTO banking.accounts (account_name, account_type, balance) VALUES ('Smith', 'savings', 2000);
INSERT INTO banking.accounts (account_name, account_type, balance) VALUES ('Smith', 'checking', 50);
```

Here are the balances for John and Smith.
<div class='copy separator-gt'>
```sql
cqlsh> select * from banking.accounts;
```
</div>
```sql
 account_name | account_type | balance
--------------+--------------+---------
         John |     checking |     100
         John |      savings |    1000
        Smith |     checking |      50
        Smith |      savings |    2000
```
<div class='copy separator-gt'>
```sql
cqlsh> SELECT SUM(balance) as Johns_balance FROM banking.accounts WHERE account_name='John';
```
</div>
```sql
 johns_balance
---------------
          1100
```
<div class='copy separator-gt'>
```sql
cqlsh> SELECT SUM(balance) as smiths_balance FROM banking.accounts WHERE account_name='Smith';
```
</div>
```sql
 smiths_balance
----------------
           2050

```


## 4. Execute a transaction

Here are a couple of examples of executing transactions.

- Let us say John transfers $200 from his savings account to his checking account. This has to be a transactional operation. This can be achieved as follows.

```{.sql .copy}
BEGIN TRANSACTION
  UPDATE banking.accounts SET balance = balance - 200 WHERE account_name='John' AND account_type='savings';
  UPDATE banking.accounts SET balance = balance + 200 WHERE account_name='John' AND account_type='checking';
END TRANSACTION;
```

If we now selected the value of John's account, we should see the amounts reflected. The total balance should be the same $1100 as before.
<div class='copy separator-gt'>
```sql
cqlsh> select * from banking.accounts where account_name='John';
```
</div>
```sql
 account_name | account_type | balance
--------------+--------------+---------
         John |     checking |     300
         John |      savings |     800
```
<div class='copy separator-gt'>
```sql
cqlsh> SELECT SUM(balance) as Johns_balance FROM banking.accounts WHERE account_name='John';
```
</div>
```sql
 johns_balance
---------------
          1100
```

Further, the checking and savings account balances for John should have been written at the same write timestamp.
<div class='copy separator-gt'>
```sql
cqlsh> select account_name, account_type, balance, writetime(balance) 
from banking.accounts where account_name='John';
```
</div>
```sql
 account_name | account_type | balance | writetime(balance)
--------------+--------------+---------+--------------------
         John |     checking |     300 |   1517898028890171
         John |      savings |     800 |   1517898028890171
```


- Now let us say John transfers the $200 from his checking account to Smith's checking account. We can accomplish that with the following transaction.

```{.sql .copy}
BEGIN TRANSACTION
  UPDATE banking.accounts SET balance = balance - 200 WHERE account_name='John' AND account_type='checking';
  UPDATE banking.accounts SET balance = balance + 200 WHERE account_name='Smith' AND account_type='checking';
END TRANSACTION;
```

We can verify the transfer was made as we intended, and also verify that the time at which the two accounts were updated are identical by performing the following query.
<div class='copy separator-gt'>
```sql
cqlsh> select account_name, account_type, balance, writetime(balance) from banking.accounts;
```
</div>
```sql
 account_name | account_type | balance | writetime(balance)
--------------+--------------+---------+--------------------
         John |     checking |     100 |   1517898167629366
         John |      savings |     800 |   1517898028890171
        Smith |     checking |     250 |   1517898167629366
        Smith |      savings |    2000 |   1517894361290020
```

The net balance for John should have decreased by $200 which that of Smith should have increased by $200.
<div class='copy separator-gt'>
```sql
cqlsh> SELECT SUM(balance) as Johns_balance FROM banking.accounts WHERE account_name='John';
```
</div>
```sql
 johns_balance
---------------
           900
```
<div class='copy separator-gt'>
```sql
cqlsh> SELECT SUM(balance) as smiths_balance FROM banking.accounts WHERE account_name='Smith';
```
</div>
```sql
 smiths_balance
----------------
           2250
```


## 5. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.
<div class='copy separator-dollar'>
```sh
$ ./bin/yb-ctl destroy
```
</div>

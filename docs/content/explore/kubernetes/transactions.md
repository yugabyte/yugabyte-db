## 1. Setup - create universe and table

If you have a previously running local universe, destroy it using the following.

```sh
$ kubectl delete -f yugabyte-statefulset.yaml
```

Start a new local cluster - by default, this will create a 3 node universe with a replication factor of 3.

```sh
$ kubectl apply -f yugabyte-statefulset.yaml
```

Check the Kubernetes dashboard to see the 3 yb-tserver and 3 yb-master pods representing the 3 nodes of the cluster.

```sh
$ minikube dashboard
```

![Kubernetes Dashboard](/images/ce/kubernetes-dashboard.png)

## 2. Create a table for transactions

Connect to cqlsh on node 1.

```sh
$ kubectl exec -it yb-tserver-0 /home/yugabyte/bin/cqlsh
```
```sh
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh>
```


Create a keyspace.

```sql
cqlsh> CREATE KEYSPACE banking;
```

Create a table with the `transactions` property set to enabled.

```sql
cqlsh> CREATE TABLE banking.accounts (
  account_name varchar,
  account_type varchar,
  balance float,
  PRIMARY KEY ((account_name), account_type)
) with transactions = { 'enabled' : true };
```

You can verify that this table has transactions enabled on it by querying the 

```sql
cqlsh> select keyspace_name, table_name, transactions from system_schema.tables
where keyspace_name='banking' AND table_name = 'accounts';
```
```sql
 keyspace_name | table_name | transactions
---------------+------------+---------------------
       banking |   accounts | {'enabled': 'true'}

(1 rows)
```


## 3. Insert sample data

Let us seed this table with some sample data.

```sql
INSERT INTO banking.accounts (account_name, account_type, balance) VALUES ('John', 'savings', 1000);
INSERT INTO banking.accounts (account_name, account_type, balance) VALUES ('John', 'checking', 100);
INSERT INTO banking.accounts (account_name, account_type, balance) VALUES ('Smith', 'savings', 2000);
INSERT INTO banking.accounts (account_name, account_type, balance) VALUES ('Smith', 'checking', 50);
```

Here are the balances for John and Smith.

```sql
cqlsh> select * from banking.accounts;
```
```sql
 account_name | account_type | balance
--------------+--------------+---------
         John |     checking |     100
         John |      savings |    1000
        Smith |     checking |      50
        Smith |      savings |    2000
```

```sql
cqlsh> SELECT SUM(balance) as Johns_balance FROM banking.accounts WHERE account_name='John';
```
```sql
 johns_balance
---------------
          1100
```

```sql
cqlsh> SELECT SUM(balance) as smiths_balance FROM banking.accounts WHERE account_name='Smith';
```
```sql
 smiths_balance
----------------
           2050

```


## 4. Execute a transaction

Here are a couple of examples of executing transactions.

- Let us say John transfers $200 from his savings account to his checking account. This has to be a transactional operation. This can be achieved as follows.

```sql
BEGIN TRANSACTION;
UPDATE banking.accounts SET balance = balance - 200 WHERE account_name='John' AND account_type='savings';
UPDATE banking.accounts SET balance = balance + 200 WHERE account_name='John' AND account_type='checking';
END TRANSACTION;
```

If we now selected the value of John's account, we should see the amounts reflected. The total balance should be the same $1100 as before.

```sh
cqlsh> select * from banking.accounts where account_name='John';
```
```sql
 account_name | account_type | balance
--------------+--------------+---------
         John |     checking |     300
         John |      savings |     800
```

```sh
cqlsh> SELECT SUM(balance) as Johns_balance FROM banking.accounts WHERE account_name='John';
```
```sql
 johns_balance
---------------
          1100
```

Further, the checking and savings account balances for John should have been written at the same write timestamp.

```sh
cqlsh> select account_name, account_type, balance, writetime(balance) 
from banking.accounts where account_name='John';
```
```sql
 account_name | account_type | balance | writetime(balance)
--------------+--------------+---------+--------------------
         John |     checking |     300 |   1517898028890171
         John |      savings |     800 |   1517898028890171
```


- Now let us say John transfers the $200 from his checking account to Smith's checking account. We can accomplish that with the following transaction.

```sql
BEGIN TRANSACTION;
UPDATE banking.accounts SET balance = balance - 200 WHERE account_name='John' AND account_type='checking';
UPDATE banking.accounts SET balance = balance + 200 WHERE account_name='Smith' AND account_type='checking';
END TRANSACTION;
```

We can verify the transfer was made as we intended, and also verify that the time at which the two accounts were updated are identical by performing the following query.

```sql
cqlsh> select account_name, account_type, balance, writetime(balance) from banking.accounts;
```
```sql
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
```
```sql
 johns_balance
---------------
           900
```

```sh
cqlsh> SELECT SUM(balance) as smiths_balance FROM banking.accounts WHERE account_name='Smith';
```
```sql
 smiths_balance
----------------
           2250
```


## 5. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```sh
$ kubectl delete -f yugabyte-statefulset.yaml
```


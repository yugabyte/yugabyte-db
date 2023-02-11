---
title: Availability of Transactions
headerTitle:  Availability of Transactions
linkTitle: Availability of Transactions
description: Simulate fault tolerance and resilience of transactions in a local YugabyteDB database universe.
headcontent: Highly available and fault tolerant transactions
aliases:
  - /explore/fault-tolerance/transactions/
  - /preview/explore/fault-tolerance/transactions/
  - /preview/explore/cloud-native/fault-tolerance/transactions/
  - /preview/explore/postgresql/fault-tolerance/transactions/
  - /preview/explore/fault-tolerance-macos/transactions/
menu:
  preview:
    identifier: transaction-availability-local
    parent: fault-tolerance-1-macos
    weight: 215
type: docs
---

[Transactions](../../../architecture/transactions/distributed-txns/) are critical to many applications and need to work through different failure scenarios.  YugabyteDB provides [high availability](../../../architecture/core-functions/high-availability/) (HA) of transactions by replicating the uncommitted values, a.k.a [provisional records](../../../architecture/transactions/distributed-txns/#provisional-records) across the [fault domains](../../../architecture/docdb-replication/replication/#fault-domains). The following examples demonstrate how YugabyteDB transactions survive common failure scenarios that could happen when a transaction is being processed.


<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../macos/transaction-availability" class="nav-link active">
      <img src="/icons/database.svg" alt="Server Icon">
      Local
    </a>
  </li>
</ul>

{{% explore-setup-single %}}

Follow the [setup instructions](../../#set-up-yugabytedb-universe) to start a single region three-node universe. To verify that the cluster is running correctly, navigate to the application UI at <http://localhost:7000/> to view the status of the nodes in the cluster.

Create the Table.
```sql
CREATE TABLE txndemo (
    k1 int,
    k2 int,
    PRIMARY KEY(k1)
);
```
Insert some sample data and determine hashcode of the primary keys.
```sql
INSERT INTO txndemo SELECT id,10 FROM generate_series(1,5) AS id;
SELECT id, upper(to_hex(yb_hash_code(id))) AS hash FROM generate_series(1,5) AS id;
 id | hash
----+------
  1 | 1210
  2 | C0C4
  3 | FCA0
  4 | 9EAF
  5 | A73
(5 rows)

```    
List the nodes via `yb-admin list_tablets ysql.yugabyte txndemo`
```
Tablet-UUID                        Key Range         Leader-IP       Leader-UUID
7e2dfb66a4654aa5b2fb133b446aaabc   [0x0000, 0x5554]  127.0.0.2:9100  4739b43f76184e1cab003b88686df290
a9b4675fdaaa4d4b949adc5e53d183bf   [0x5555, 0xAAA9]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
a8c50129f63642459a02ed4ee492a1f3   [0x0000, 0x5554]  127.0.0.1:9100  6789e52b1c334844a66078fe9fdf95fa
```

From the hash ranges listed on the tablet-servers page at <http://localhost:7000/tablet-servers> and [yb-admin](../../../admin/yb-admin/) output, we can see that the row with `k1=1` resides on node `127.0.0.2`. We will use this row for the transactions.


## Failure Scenario 1
In this example, you are going to see how a transaction successfully completes when the node that has just received a [provisional write](../../../architecture/transactions/distributed-txns/#provisional-records) fails.

Connect to `127.0.0.3` and start a simple transaction to update the value of row `k1=1` to `20`.
```sql
ysqlsh -h 127.0.0.3  -U yugabyte -d yugabyte
yugabyte@yugabyte=# BEGIN;
BEGIN
Time: 2.047 ms
yugabyte@yugabyte=# UPDATE txndemo set k2=20 where k1=1;
UPDATE 1
Time: 51.513 ms
```    
Stop the node:`127.0.0.2`.
```sh
yugabyted stop --base_dir=/tmp/ybd2
```

List the nodes via `yb-admin list_tablets ysql.yugabyte txndemo`
```
Tablet-UUID                        Key Range         Leader-IP       Leader-UUID
7e2dfb66a4654aa5b2fb133b446aaabc   [0x0000, 0x5554]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
a9b4675fdaaa4d4b949adc5e53d183bf   [0x5555, 0xAAA9]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
a8c50129f63642459a02ed4ee492a1f3   [0x0000, 0x5554]  127.0.0.1:9100  6789e52b1c334844a66078fe9fdf95fa
```
Notice that `127.0.0.2` is gone from the list and `127.0.0.3` is now the leader for the tablet that was in `127.0.0.2`

Commit the transaction.
```sql
yugabyte@yugabyte=# COMMIT;
COMMIT
Time: 6.243 ms
```    
We can see that even though the node `127.0.0.2` failed after receiving the provisional write, the transaction has succeeded.

## Failure Scenario 2
In this example, you are going to see how a transaction successfully completes when the node that is about to receive a provisional write fails.

List the nodes via `yb-admin list_tablets ysql.yugabyte txndemo`
```
Tablet-UUID                        Key Range         Leader-IP       Leader-UUID
7e2dfb66a4654aa5b2fb133b446aaabc   [0x0000, 0x5554]  127.0.0.2:9100  4739b43f76184e1cab003b88686df290
a9b4675fdaaa4d4b949adc5e53d183bf   [0x5555, 0xAAA9]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
a8c50129f63642459a02ed4ee492a1f3   [0x0000, 0x5554]  127.0.0.1:9100  6789e52b1c334844a66078fe9fdf95fa
```    
Connect to `127.0.0.3` and start a simple transaction to update the value of row `k1=1` to `30`.
```sql
ysqlsh -h 127.0.0.3  -U yugabyte -d yugabyte
yugabyte@yugabyte=# BEGIN;
BEGIN
Time: 2.047 ms
```    

Stop the node:`127.0.0.2`.
```sh
yugabyted stop --base_dir=/tmp/ybd2
```

List the nodes via `yb-admin list_tablets ysql.yugabyte txndemo` to verify node:`127.0.0.2` is gone
```
Tablet-UUID                        Key Range         Leader-IP       Leader-UUID
7e2dfb66a4654aa5b2fb133b446aaabc   [0x0000, 0x5554]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
a9b4675fdaaa4d4b949adc5e53d183bf   [0x5555, 0xAAA9]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
a8c50129f63642459a02ed4ee492a1f3   [0x0000, 0x5554]  127.0.0.1:9100  6789e52b1c334844a66078fe9fdf95fa
```   
Commit the transaction
```sql
yugabyte@yugabyte=# UPDATE txndemo set k2=30 where k1=1; COMMIT;
UPDATE 1
Time: 1728.246 ms (00:01.728)
COMMIT
Time: 2.964 ms
```    
You can see that even though the node `127.0.0.2` failed before receiving the provisional write, the transaction has succeeded. This is because a new leader (node:`127.0.0.3`) was quickly elected.
    
## Failure of Transaction Manager(Coordinator)
The node to which a client connects to, acts as the coordinator for the transaction. In this example you will  see how a transaction will abort when the coordinator fails.

List the nodes via `yb-admin list_tablets ysql.yugabyte txndemo`
```
Tablet-UUID                       Key Range         Leader-IP       Leader-UUID
7e2dfb66a4654aa5b2fb133b446aaabc  [0x0000, 0x5554]  127.0.0.2:9100  4739b43f76184e1cab003b88686df290
a9b4675fdaaa4d4b949adc5e53d183bf  [0x5555, 0xAAA9]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
a8c50129f63642459a02ed4ee492a1f3  [0x0000, 0x5554]  127.0.0.1:9100  6789e52b1c334844a66078fe9fdf95fa
```    
Connect to `127.0.0.3` and start a simple transaction to update the value of row `k1=1` to `40`.
```sql
ysqlsh -h 127.0.0.3  -U yugabyte -d yugabyte
yugabyte@yugabyte=# BEGIN;
BEGIN
Time: 2.047 ms
yugabyte@yugabyte=# UPDATE txndemo set k2=40 where k1=1;
UPDATE 1
Time: 50.624 ms
 ```

Stop the node:`127.0.0.3` 
```sh
yugabyted stop --base_dir=/tmp/ybd3
```

Commit the transaction
```sql
yugabyte@yugabyte=# COMMIT;
FATAL:  57P01: terminating connection due to unexpected postmaster exit
LOCATION:  secure_read, be-secure.c:199
server closed the connection unexpectedly
  This probably means the server terminated abnormally
  before or while processing the request.
The connection to the server was lost. Attempting reset: Failed.
Time: 2.499 ms
```

The client received an error response from the server. Connect to a different node and check the value.
```sql
ysqlsh -h 127.0.0.1  -U yugabyte -d yugabyte
yugabyte@yugabyte=# select * from txndemo where k1=1;
 k1 | k2
----+----
  1 | 30
(1 row)
```

The row still has the old value of `30`. The transaction has failed. When the transaction coordinator fails before commit happens, the transaction is lost. At this point, it will be the application's responsibility to retry the transaction.


### Clean up

You can shut down the local universe that you created as follows:

```sh
yugabyted destroy --base_dir=/tmp/ybd1
yugabyted destroy --base_dir=/tmp/ybd2
yugabyted destroy --base_dir=/tmp/ybd3
```

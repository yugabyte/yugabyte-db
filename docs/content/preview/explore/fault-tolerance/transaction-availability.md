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

[Transactions](../../../architecture/transactions/distributed-txns/) are critical to many applications and need to work through different failure scenarios.  YugabyteDB provides [high availability](../../../architecture/core-functions/high-availability/) (HA) of transactions by replicating the uncommitted values, a.k.a [provisional records](../../../architecture/transactions/distributed-txns/#provisional-records) across the [fault domains](../../../architecture/docdb-replication/replication/#fault-domains). The following examples demonstrate how YugabyteDB transactions survive common failure scenarios that could happen when a transaction is being processed. Some of the scenarios are:
- The node that has received the provisional write fails (Handled by YugabyteDB)
- The node that is about to receive the provisional write fails (Handled by YugabyteDB)
- The transaction co-ordinator fails (Retry by client)


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
```

### Identifying the Key location
For the purpose of the examples, you would need to identify which node holds a specific row, so that you
can kill the correct node to see what is happening. For this one needs to get the hash code of the primary key using the `yb_hash_code(id)` function and co-relate with the `yb-admin` output to figure out where that key is located. For the examples we will focus on the row with `k1=1`.

```sql
SELECT id, upper(to_hex(yb_hash_code(id))) AS hash FROM generate_series(1,5) AS id;
```
```output.sql
 id | hash
----+------
  1 | 1210
  2 | C0C4
  3 | FCA0
  4 | 9EAF
  5 | A73
(5 rows)
```    
List the nodes via 
```sh
yb-admin list_tablets ysql.yugabyte txndemo
```

```output.sh
Tablet-UUID                        Key Range         Leader-IP       Leader-UUID
7e2dfb66a4654aa5b2fb133b446aaabc   [0x0000, 0x5554]  127.0.0.2:9100  4739b43f76184e1cab003b88686df290
a9b4675fdaaa4d4b949adc5e53d183bf   [0x5555, 0xAAA9]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
a8c50129f63642459a02ed4ee492a1f3   [0x0000, 0x5554]  127.0.0.1:9100  6789e52b1c334844a66078fe9fdf95fa
```
 
From the hash ranges listed on the tablet-servers page at <http://localhost:7000/tablet-servers> and [yb-admin](../../../admin/yb-admin/) output, we can see that the row with `k1=1` whose hash code is `1210` resides on node `127.0.0.2` as that node has the tablet which contains the key range `[0x0000, 0x5554]`. 


## Failure Scenario 1
In this example, you are going to see how a transaction successfully completes when the node that has just received a [provisional write](../../../architecture/transactions/distributed-txns/#provisional-records) fails.

Connect to `127.0.0.3` and start a simple transaction to update the value of row `k1=1` to `20`.
```output.sql
ysqlsh -h 127.0.0.3  -U yugabyte -d yugabyte
yugabyte@yugabyte=# BEGIN;
BEGIN
Time: 2.047 ms
yugabyte@yugabyte=# UPDATE txndemo set k2=20 where k1=1;
UPDATE 1
Time: 51.513 ms
```    
Stop the node:`127.0.0.2`. _Note_: We are stopping `127.0.0.2` as that is the node that contains `k1=1` which we figured from the steps in the [Identifying the key location](#identifying-the-key-location) section.
```sh
yugabyted stop --base_dir=/tmp/ybd2
```

List the nodes via 
```sh
yb-admin list_tablets ysql.yugabyte txndemo
```

```output.sh
Tablet-UUID                        Key Range         Leader-IP       Leader-UUID
7e2dfb66a4654aa5b2fb133b446aaabc   [0x0000, 0x5554]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
a9b4675fdaaa4d4b949adc5e53d183bf   [0x5555, 0xAAA9]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
a8c50129f63642459a02ed4ee492a1f3   [0x0000, 0x5554]  127.0.0.1:9100  6789e52b1c334844a66078fe9fdf95fa
```
Notice that `127.0.0.2` is gone from the list and `127.0.0.3` is now the leader for the tablet that was in `127.0.0.2`(`7e2dfb66a..`)

Commit the transaction.
```output.sql
yugabyte@yugabyte=# COMMIT;
COMMIT
Time: 6.243 ms
yugabyte@yugabyte=# SELECT * from txndemo where k1=1;
 k1 | k2
----+----
  1 | 20
(1 row)
```
We can see that even though the node `127.0.0.2` failed after receiving the provisional write, the transaction has succeeded(value has been updated to `20`).

## Failure Scenario 2
In this example, you are going to see how a transaction successfully completes when the node that is about to receive a provisional write fails.

List the nodes via 
```sh
yb-admin list_tablets ysql.yugabyte txndemo
```
```output.sh
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

Stop the node:`127.0.0.2`. _Note_: We are stopping `127.0.0.2` as that is the node that contains `k1=1` which we figured from the steps in the [Identifying the key location](#identifying-the-key-location) section.
```sh
yugabyted stop --base_dir=/tmp/ybd2
```

List the nodes to verify node:`127.0.0.2` is gone.
```sh
yb-admin list_tablets ysql.yugabyte txndemo
```
```output.sh
Tablet-UUID                        Key Range         Leader-IP       Leader-UUID
7e2dfb66a4654aa5b2fb133b446aaabc   [0x0000, 0x5554]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
a9b4675fdaaa4d4b949adc5e53d183bf   [0x5555, 0xAAA9]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
a8c50129f63642459a02ed4ee492a1f3   [0x0000, 0x5554]  127.0.0.1:9100  6789e52b1c334844a66078fe9fdf95fa
```   
Commit the transaction
```output.sql
yugabyte@yugabyte=# UPDATE txndemo set k2=30 where k1=1; COMMIT;
UPDATE 1
Time: 1728.246 ms (00:01.728)
COMMIT
Time: 2.964 ms
yugabyte@yugabyte=# SELECT * from txndemo where k1=1;
 k1 | k2
----+----
  1 | 30
(1 row)
```    
You can see that even though the node `127.0.0.2` failed before receiving the provisional write, the transaction has succeeded(value has been updated to `30`). This is because a new leader (node:`127.0.0.3`) was quickly elected.
    
## Failure of Transaction Manager(Coordinator)
The node to which a client connects to, acts as the coordinator for the transaction. You have seen how YugabyteDB is inherently resilient to node failures in the above two scenarios. In this example you will see how a transaction will abort when the coordinator fails. More details on the role of the transaction manager can be found in [Transactional I/O](../../architecture/transactions/transactional-io-path/#client-requests-transaction) section.

List the nodes via `yb-admin list_tablets ysql.yugabyte txndemo`
```
Tablet-UUID                       Key Range         Leader-IP       Leader-UUID
7e2dfb66a4654aa5b2fb133b446aaabc  [0x0000, 0x5554]  127.0.0.2:9100  4739b43f76184e1cab003b88686df290
a9b4675fdaaa4d4b949adc5e53d183bf  [0x5555, 0xAAA9]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
a8c50129f63642459a02ed4ee492a1f3  [0x0000, 0x5554]  127.0.0.1:9100  6789e52b1c334844a66078fe9fdf95fa
```    
Connect to `127.0.0.3` and start a simple transaction to update the value of row `k1=1` to `40`.
```output.sql
ysqlsh -h 127.0.0.3  -U yugabyte -d yugabyte
yugabyte@yugabyte=# BEGIN;
BEGIN
Time: 2.047 ms
yugabyte@yugabyte=# UPDATE txndemo set k2=40 where k1=1;
UPDATE 1
Time: 50.624 ms
 ```

Stop the node:`127.0.0.3`. _Note_: We are stopping `127.0.0.3` as that is the node that we have connected to.
```sh
yugabyted stop --base_dir=/tmp/ybd3
```

Commit the transaction
```output.sql
yugabyte@yugabyte=# COMMIT;
FATAL:  57P01: terminating connection due to unexpected postmaster exit
server closed the connection unexpectedly
  This probably means the server terminated abnormally
  before or while processing the request.
The connection to the server was lost. Attempting reset: Failed.
Time: 2.499 ms
```

The client received an error response from the server. Connect to a different node and check the value.
```sql
ysqlsh -h 127.0.0.1  -U yugabyte -d yugabyte
select * from txndemo where k1=1;
```
```output.sql
 k1 | k2
----+----
  1 | 30
(1 row)
```

The row did not get the intended value of `40` and still has the old value of `30`. The transaction did not go through. When the transaction coordinator fails before commit happens, the transaction is lost. At this point, it will be the application's responsibility to retry the transaction.

## Conclusion
As replication is a first-class feature in YugabyteDB, it naturally handles node failures during transactions.During the failure of the transacation manager, even though the provisional records are replicated, the maintains the co-relation between the client and a transaction-id, which the client is unaware of. For further reading, to understand how YugabyteDB is able to handle failures during transcation processing and the potential impact of failures, take a look at the [Distribution Transactions](../../../architecture/transactions/distributed-txns/#impact-of-failures) page.

### Clean up

You can shut down the local universe that you created as follows:

```sh
yugabyted destroy --base_dir=/tmp/ybd1
yugabyted destroy --base_dir=/tmp/ybd2
yugabyted destroy --base_dir=/tmp/ybd3
```

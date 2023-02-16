---
title: Availability of Transactions
headerTitle:  Availability of Transactions
linkTitle: Availability of Transactions
description: Simulate fault tolerance and resilience of transactions in a local YugabyteDB database universe.
headcontent: Highly available and fault tolerant transactions
menu:
  preview:
    identifier: transaction-availability-local
    parent: fault-tolerance-1-macos
    weight: 215
type: docs
---

[Transactions](../../../architecture/transactions/distributed-txns/) are critical to many applications and need to work through different failure scenarios. YugabyteDB provides [high availability](../../../architecture/core-functions/high-availability/) (HA) of transactions by replicating the uncommitted values, a.k.a [provisional records](../../../architecture/transactions/distributed-txns/#provisional-records) across the [fault domains](../../../architecture/docdb-replication/replication/#fault-domains). The following examples demonstrate how YugabyteDB transactions survive common failure scenarios that could happen when a transaction is being processed. Some of the scenarios are:

- The node that has received the provisional write fails (Handled by YugabyteDB)
- The node that is about to receive the provisional write fails (Handled by YugabyteDB)
- The transaction manager fails (Retry by client)

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

Insert some sample data and determine the hashcode of the primary keys.

```sql
INSERT INTO txndemo SELECT id,10 FROM generate_series(1,5) AS id;
```

### Identify the key location

For the examples, you need to identify which node holds a specific row, so that you
can shut down the correct node to see what is happening. You can get the hash code of the primary key using the `yb_hash_code(id)` function, and correlate it with the `yb-admin` output to figure out where that key is located. The examples on this page use the row with `k1=1`.

    ```sql
    SELECT id, upper(to_hex(yb_hash_code(id))) AS hash FROM generate_series(1,5) AS id;
    ```

    ```output
    id | hash
    ----+------
    1 | 1210
    2 | C0C4
    3 | FCA0
    4 | 9EAF
    5 | A73
    (5 rows)
    ```

List the nodes:

    ```sh
    $ yb-admin list_tablets ysql.yugabyte txndemo
    ```

    ```output.sh
    Tablet-UUID                        Key Range         Leader-IP       Leader-UUID
    7e2dfb66a4654aa5b2fb133b446aaabc   [0x0000, 0x5554]  127.0.0.2:9100  4739b43f76184e1cab003b88686df290
    a9b4675fdaaa4d4b949adc5e53d183bf   [0x5555, 0xAAA9]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
    a8c50129f63642459a02ed4ee492a1f3   [0x0000, 0x5554]  127.0.0.1:9100  6789e52b1c334844a66078fe9fdf95fa
    ```

From the hash ranges listed on the tablet-servers page at <http://localhost:7000/tablet-servers> and the [yb-admin](../../../admin/yb-admin/) output, you can determine that the row with `k1=1` whose hash code is `1210` resides on node `127.0.0.2`, as that node has the tablet containing the key range `[0x0000, 0x5554]`.

## Failure Scenario 1

In this example, you are going to see how a transaction completes when the node that has just received a [provisional write](../../../architecture/transactions/distributed-txns/#provisional-records) fails.

1. Connect to `127.0.0.3`.

    ```sh
    ysqlsh -h 127.0.0.3  -U yugabyte -d yugabyte
    ```

1. Start a transaction to update the value of row `k1=1` to `20`.

    ```sql
    BEGIN;
    ```

    ```output
    BEGIN
    Time: 2.047 ms
    ```

    ```sql
    UPDATE txndemo set k2=20 where k1=1;
    ```

    ```output
    UPDATE 1
    Time: 51.513 ms
    ```

1. Stop the node at `127.0.0.2`. (This is the node from [Identify the key location](#identify-the-key-location).)

    ```sh
    yugabyted stop --base_dir=/tmp/ybd2
    ```

1. List the nodes.

    ```sh
    yb-admin list_tablets ysql.yugabyte txndemo
    ```

    ```output.sh
    Tablet-UUID                        Key Range         Leader-IP       Leader-UUID
    7e2dfb66a4654aa5b2fb133b446aaabc   [0x0000, 0x5554]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
    a9b4675fdaaa4d4b949adc5e53d183bf   [0x5555, 0xAAA9]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
    a8c50129f63642459a02ed4ee492a1f3   [0x0000, 0x5554]  127.0.0.1:9100  6789e52b1c334844a66078fe9fdf95fa
    ```

    Notice that `127.0.0.2` is gone from the list and `127.0.0.3` is now the leader for the tablet that was in `127.0.0.2` (`7e2dfb66a..`)

1. Commit the transaction.

    ```sql
    COMMIT;
    ```

    ```ouput.sql
    COMMIT
    Time: 6.243 ms
    ```

1. Check the value of the row at `k1=1`.

    ```sql
    # SELECT * from txndemo where k1=1;
    ```

    ```output
     k1 | k2
    ----+----
      1 | 20
    (1 row)
    ```

The transaction has succeeded: even though the node at `127.0.0.2` failed after receiving the provisional write, the value has been updated to `20`.

## Failure Scenario 2

In this example, you are going to see how a transaction completes when the node that is about to receive a provisional write fails.

1. List the nodes.

    ```sh
    yb-admin list_tablets ysql.yugabyte txndemo
    ```

    ```output.sh
    Tablet-UUID                        Key Range         Leader-IP       Leader-UUID
    7e2dfb66a4654aa5b2fb133b446aaabc   [0x0000, 0x5554]  127.0.0.2:9100  4739b43f76184e1cab003b88686df290
    a9b4675fdaaa4d4b949adc5e53d183bf   [0x5555, 0xAAA9]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
    a8c50129f63642459a02ed4ee492a1f3   [0x0000, 0x5554]  127.0.0.1:9100  6789e52b1c334844a66078fe9fdf95fa
    ```

1. Connect to `127.0.0.3`.

    ```sh
    ysqlsh -h 127.0.0.3  -U yugabyte -d yugabyte
    ```

1. Start a transaction.

    ```sql
    BEGIN;
    ```

    ```output
    BEGIN
    Time: 2.047 ms
    ```

1. Stop the node at `127.0.0.2`. (This is the node from [Identify the key location](#identify-the-key-location).)

    ```sh
    yugabyted stop --base_dir=/tmp/ybd2
    ```

1. List the nodes to verify the node at `127.0.0.2` is gone.

    ```sh
    yb-admin list_tablets ysql.yugabyte txndemo
    ```

    ```output.sh
    Tablet-UUID                        Key Range         Leader-IP       Leader-UUID
    7e2dfb66a4654aa5b2fb133b446aaabc   [0x0000, 0x5554]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
    a9b4675fdaaa4d4b949adc5e53d183bf   [0x5555, 0xAAA9]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
    a8c50129f63642459a02ed4ee492a1f3   [0x0000, 0x5554]  127.0.0.1:9100  6789e52b1c334844a66078fe9fdf95fa
    ```

1. Complete and commit the transaction.

    ```sql
    UPDATE txndemo set k2=30 where k1=1; COMMIT;
    ```

    ```output
    UPDATE 1
    Time: 1728.246 ms (00:01.728)
    COMMIT
    Time: 2.964 ms
    ```

1. Check the value of the row at `k1=1`.

    ```sql
    SELECT * from txndemo where k1=1;
    ```

    ```output
     k1 | k2
    ----+----
      1 | 30
    (1 row)
    ```

The transaction has succeeded: even though the node at `127.0.0.2` failed after receiving the provisional write, the value has been updated to `30`. The transaction succeeded because a new leader (the node at `127.0.0.3`) was quickly elected.

## Transaction Manager failure

The node to which a client connects acts as the manager for the transaction. You have seen how YugabyteDB is inherently resilient to node failures in the above two scenarios. In this example, you will see how a transaction will abort when the manager fails. More details on the role of the transaction manager can be found in [Transactional I/O](../../architecture/transactions/transactional-io-path/#client-requests-transaction) section.

1. List the nodes.

    ```sh
    yb-admin list_tablets ysql.yugabyte txndemo
    ```

    ```output.sh
    Tablet-UUID                       Key Range         Leader-IP       Leader-UUID
    7e2dfb66a4654aa5b2fb133b446aaabc  [0x0000, 0x5554]  127.0.0.2:9100  4739b43f76184e1cab003b88686df290
    a9b4675fdaaa4d4b949adc5e53d183bf  [0x5555, 0xAAA9]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
    a8c50129f63642459a02ed4ee492a1f3  [0x0000, 0x5554]  127.0.0.1:9100  6789e52b1c334844a66078fe9fdf95fa
    ```

1. Connect to the node at `127.0.0.3`.

    ```sh
    ysqlsh -h 127.0.0.3  -U yugabyte -d yugabyte
    ```

1. Start a transaction to update the value of row `k1=1` to `40`.

    ```sql
    BEGIN;
    ```

    ```output
    BEGIN
    Time: 2.047 ms
    ```

    ```sql
    UPDATE txndemo set k2=40 where k1=1;
    ```

    ```output
    UPDATE 1
    Time: 50.624 ms
    ```

1. Stop the node at `127.0.0.3` (the node that we have connected to).

    ```sh
    yugabyted stop --base_dir=/tmp/ybd3
    ```

1. Commit the transaction.

    ```sql
    COMMIT;
    ```

    Note that the client receives an error response from the server:

    ```output
    FATAL:  57P01: terminating connection due to unexpected postmaster exit
    server closed the connection unexpectedly
      This probably means the server terminated abnormally
      before or while processing the request.
    The connection to the server was lost. Attempting reset: Failed.
    Time: 2.499 ms
    ```

1. Connect to a different node and check the value.

    ```sh
    ysqlsh -h 127.0.0.1  -U yugabyte -d yugabyte
    ```

    ```sql
    SELECT * from txndemo where k1=1;
    ```

    ```output
     k1 | k2
    ----+----
      1 | 30
    (1 row)
    ```

The transaction has failed: the row did not get the intended value of `40`, and still has the old value of `30`. When the transaction manager fails before a commit happens, the transaction is lost. At this point, it's the application's responsibility to retry the transaction.

### Clean up

You can shut down the local cluster that you created as follows:

    ```sh
    yugabyted destroy --base_dir=/tmp/ybd1
    yugabyted destroy --base_dir=/tmp/ybd2
    yugabyted destroy --base_dir=/tmp/ybd3
    ```

## Conclusion

Replication is a first-class feature in YugabyteDB and naturally handles node failures during transactions. During a transaction manager failure, even though provisional records are replicated, the maintains the correlation between the client and a transaction ID, of which the client is unaware.

To better understand how YugabyteDB handles failures during transaction processing, and the potential impact of failures, see the [Distributed transactions](../../../architecture/transactions/distributed-txns/#impact-of-failures) page.

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

[Transactions](../../../architecture/transactions/distributed-txns/) are critical to many applications and need to work through different failure scenarios. YugabyteDB provides [high availability](../../../architecture/core-functions/high-availability/) (HA) of transactions by replicating the uncommitted values, also known as [provisional records](../../../architecture/transactions/distributed-txns/#provisional-records) across the [fault domains](../../../architecture/docdb-replication/replication/#fault-domains).

The following examples demonstrate how YugabyteDB transactions survive common failure scenarios that could happen when a transaction is being processed. In the examples, we will introduce different node failure scenarios and see how YugabyteDB handles the failures. Some of the scenarios are:

- The node that has received the provisional write fails (handled by YugabyteDB)
- The node that is about to receive the provisional write fails (handled by YugabyteDB)
- The node that the client is connected to fails (retry by client)

## Prerequisites

### Setup

- Local three-node YugabyteDB universe. See [Set up YugabyteDB universe](../../#set-up-yugabytedb-universe) to create your multi-node universe.

### Identifying the location of a row

For the following examples, you need to identify which node holds a specific row, so that you can shut down the correct node to see what is happening. Perform the following steps to identify the row location:

- Connect to your universe using [ysqlsh](../../../admin/ysqlsh/#starting-ysqlsh), and create a table using the following command:

    ```sql
    CREATE TABLE txndemo (
        k1 int,
        k2 int,
        PRIMARY KEY(k1)
    );
    ```

- Insert sample data and determine the hash code of the primary keys using the following command:

    ```sql
    INSERT INTO txndemo SELECT id,10 FROM generate_series(1,5) AS id;
    ```

- Fetch the hash code of the primary key k1 using the [yb_hash_code(id)](../../../api/ysql/exprs/func_yb_hash_code/) function, and correlate it with the `yb-admin` output to figure out where that key is located. The examples on this page use the row with `k1=1`.

    ```sql
    SELECT id, upper(to_hex(yb_hash_code(id))) AS hash FROM generate_series(1,5) AS id;
    ```

    ```output.sql {hl_lines=[3]}
    id | hash
    ---+------
    1  | 1210
    2  | C0C4
    3  | FCA0
    4  | 9EAF
    5  | A73
    (5 rows)
    ```

- From your YugabyteDB home directory, list the nodes using the following command:

    ```sh
    ./bin/yb-admin list_tablets ysql.yugabyte txndemo
    ```

    ```output.sh
    Tablet-UUID                       Key Range         Leader-IP       Leader-UUID
    7e2dfb66a4654aa5b2fb133b446aaabc  [0x0000, 0x5554]  127.0.0.2:9100  4739b43f76184e1cab003b88686df290
    a9b4675fdaaa4d4b949adc5e53d183bf  [0x5555, 0xAAA9]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
    a8c50129f63642459a02ed4ee492a1f3  [0x0000, 0x5554]  127.0.0.1:9100  6789e52b1c334844a66078fe9fdf95fa
    ```

From the hash ranges listed on the [tablet-servers](http://localhost:7000/tablet-servers) page, and the [yb-admin](../../../admin/yb-admin/) output, you can determine that the row with `k1=1` whose hash code is `1210` resides on node `127.0.0.2`, as that node has the tablet containing the key range `[0x0000, 0x5554]`.

## Failure of a node after receiving a write

During a transaction, when a row is updated, YugabyteDB sends the modified row (also known as [Provisional Records](../../../architecture/transactions/distributed-txns/#provisional-records)) to the node containing the row that is being modified. In this example, you can see how a transaction completes when the node that has just received a provisional write fails.

Because the row with `k1=1` is located on the node `127.0.0.2` (via [Identifying the row location](#identifing-the-location-of-a-row)), connect to node `127.0.0.3` as you have to stop the node `127.0.0.2` before committing the transaction in the following example.

1. Connect to `127.0.0.3` using the following ysqlsh command:

    ```sh
    ./bin/ysqlsh -h 127.0.0.3  -U yugabyte -d yugabyte
    ```

1. Start a transaction to update the value of row `k1=1` to `20` using the following commands:

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

    The update will succeed. This means that the updated row with value `k2=20` has been sent to node `127.0.0.2`, but not yet committed.

1. From another terminal of your YugabyteDB home directory, stop the node at `127.0.0.2`, as this is the node that has received the modified row.

    ```sh
    ./bin/yugabyted stop --base_dir=/tmp/ybd2
    ```

1. Verify that the node at `127.0.0.2` is not active and that a new leader has been elected for the row with `k1=1`. List the nodes as follows:

    ```sh
    ./bin/yb-admin list_tablets ysql.yugabyte txndemo
    ```

    ```output.sh
    Tablet-UUID                        Key Range         Leader-IP       Leader-UUID
    7e2dfb66a4654aa5b2fb133b446aaabc   [0x0000, 0x5554]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
    a9b4675fdaaa4d4b949adc5e53d183bf   [0x5555, 0xAAA9]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
    a8c50129f63642459a02ed4ee492a1f3   [0x0000, 0x5554]  127.0.0.1:9100  6789e52b1c334844a66078fe9fdf95fa
    ```

    Notice that `127.0.0.2` is gone from the list and `127.0.0.3` is now the leader for the tablet that was in `127.0.0.2` (`7e2dfb66a..`)

1. Commit the transaction as follows:

    ```sh
    ./bin/ysqlsh -h 127.0.0.3  -U yugabyte -d yugabyte
    ```

    ```sql
    COMMIT;
    ```

    ```output.sql
    COMMIT
    Time: 6.243 ms
    ```

    The transaction succeeds even though the node at `127.0.0.2` failed after receiving the provisional write, and the row value updates to `20`. This is because, the provisional writes were replicated to the follower tablets and when the leader failed, a new leader that was quickly elected already had the provisional writes, which enabled the transaction to continue further without disruption.

1. Check the value of the row at `k1=1` using the following command:

    ```sql
    SELECT * from txndemo where k1=1;
    ```

    ```output
     k1 | k2
    ----+----
      1 | 20
    (1 row)
    ```

    The row with `k1=1` has the new value of `k2=20`, confirming the completion of the transaction.

1. From another terminal of your YugabyteDB home directory, restart the node at `127.0.0.2` using the following command:

    ```sh
    ./bin/yugabyted start --base_dir=/tmp/ybd2 --join=127.0.0.1
    ```

## Failure of a node before receiving a write

As mentioned in the preceding example, when a row is updated during a transaction, YugabyteDB sends the modified row to the node the row that is being modified.

In this example, you can see how a transaction completes when the node that is about to receive a provisional write fails. You will take down node `127.0.0.2` as that node has the row with `k1=1`.

1. Connect to `127.0.0.3` as follows:

    ```sh
    ./bin/ysqlsh -h 127.0.0.3  -U yugabyte -d yugabyte
    ```

1. Start a transaction as follows:

    ```sql
    BEGIN;
    ```

    ```output.sql
    BEGIN
    Time: 2.047 ms
    ```

    The transaction is started, but you have not yet modified the row. So at this point, no provisional records have been sent to node `127.0.0.2`.

1. From another terminal of your YugabyteDB home directory, stop the node at `127.0.0.2`. (This is the node from [Identifying the row location](#identifing-the-location-of-a-row).)

    ```sh
    ./bin/yugabyted stop --base_dir=/tmp/ybd2
    ```

1. List the nodes to verify the node at `127.0.0.2` is gone from the tablet list using the following command:

    ```sh
    ./bin/yb-admin list_tablets ysql.yugabyte txndemo
    ```

    ```output.sh
    Tablet-UUID                        Key Range         Leader-IP       Leader-UUID
    7e2dfb66a4654aa5b2fb133b446aaabc   [0x0000, 0x5554]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
    a9b4675fdaaa4d4b949adc5e53d183bf   [0x5555, 0xAAA9]  127.0.0.3:9100  7402fbc9c6384d80bb7bcd09b89dbca9
    a8c50129f63642459a02ed4ee492a1f3   [0x0000, 0x5554]  127.0.0.1:9100  6789e52b1c334844a66078fe9fdf95fa
    ```

    Notice that the node `127.0.0.2` is gone and a new leader `127.0.0.3` has been elected for the tablet (`7e2dfb66a..`) which was in node `127.0.0.2`.

1. Complete and commit the transaction as follows:

    ```sql
    UPDATE txndemo set k2=30 where k1=1;
    COMMIT;
    ```

    ```output.sql
    UPDATE 1
    Time: 1728.246 ms (00:01.728)
    COMMIT
    Time: 2.964 ms
    ```

    The transaction succeeds even though the node at `127.0.0.2` failed before receiving the provisional write, and the value updates to `30`. The transaction succeeds because a new leader (the node at `127.0.0.3`) gets quickly elected.

1. Check the value of the row at `k1=1` using the following command:

    ```sql
    SELECT * from txndemo where k1=1;
    ```

    ```output
     k1 | k2
    ----+----
      1 | 30
    (1 row)
    ```

    The row with `k1=1` has the new value of `k2=30`, confirming the completion of the transaction.

1. From another terminal of your YugabyteDB home directory, restart the node at `127.0.0.2` using the following command:

    ```sh
    ./bin/yugabyted start --base_dir=/tmp/ybd2 --join=127.0.0.1
    ```

## Failure of the node to which a client has connected

The node to which a client connects acts as the manager for the transaction. The transaction manager coordinates the flow of transaction and maintains the corelation between the client and the transaction-id (a unique identifier for each transaction). YugabyteDB is inherently resilient to node failures as mentioned in the previous two scenarios.

In this example, you can see how a transaction will abort when the transaction manager fails. For more details on the role of the transaction manager, see [Transactional I/O](../../../architecture/transactions/transactional-io-path/#client-requests-transaction).

1. Connect to the node at `127.0.0.3` as follows:

    ```sh
    ./bin/ysqlsh -h 127.0.0.3  -U yugabyte -d yugabyte
    ```

1. Start a transaction to update the value of row `k1=1` to `40` as follows:

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

1. From another terminal of your YugabyteDB home directory, stop the node at `127.0.0.3` (the node that you have connected to) as follows:

    ```sh
    ./bin/yugabyted stop --base_dir=/tmp/ybd3
    ```

1. Commit the transaction as follows:

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

1. From another terminal of your YugabyteDB home directory, connect to a different node and check the value as follows:

    ```sh
    ./bin/ysqlsh -h 127.0.0.1  -U yugabyte -d yugabyte
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

    The transaction fails; the row did not get the intended value of `40`, and still has the old value of `30`. When the transaction manager fails before a commit happens, the transaction is lost. At this point, it's the application's responsibility to restart the transaction.

## Clean up

You can shut down the local cluster that you created as follows:

```sh
./bin/yugabyted destroy --base_dir=/tmp/ybd1
./bin/yugabyted destroy --base_dir=/tmp/ybd2
./bin/yugabyted destroy --base_dir=/tmp/ybd3
```

## Conclusion

Replication is a first-class feature in YugabyteDB and naturally handles node failures during transactions. Only during a transaction manager failure, even though provisional records are replicated, the client has to restart the transaction as it is unaware of the transaction ID.

To better understand how YugabyteDB handles failures and its impact during transaction processing, refer to the [Impact of failures](../../../architecture/transactions/distributed-txns/#impact-of-failures).

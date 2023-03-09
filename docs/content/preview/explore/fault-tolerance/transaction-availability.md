---
title: High availability of transactions
headerTitle:  High availability of transactions
linkTitle: HA of transactions
description: Simulate fault tolerance and resilience of transactions in a local YugabyteDB database universe.
headcontent: Highly available and fault tolerant transactions
menu:
  preview:
    identifier: transaction-availability-local
    parent: fault-tolerance
    weight: 20
type: docs
---

[Transactions](../../../architecture/transactions/distributed-txns/) and resilience are both critical features of YugabyteDB, and they are designed to work together. This means that transactions can make progress even under some failure scenarios.

The following examples demonstrate how YugabyteDB transactions survive common failure scenarios that could happen when a transaction is being processed. In the examples, you execute a client-driven transaction that updates a single row, with failure scenarios as shown in the following table.

| Scenario | Description |
| :------- | :---------- |
| Node failure just before a transaction executes a statement | The node to which a statement is about to be sent fails just before it is executed. YugabyteDB handles this automatically. |
| Node failure just after a transaction executes a statement | The node to which the statement has been sent fails before the transaction is committed. YugabyteDB handles this automatically. |
| Failure of the node to which a client has connected | The node to which the client is connected fails after a statement but before the transaction has been committed. The database returns a standard error which is handled using retry logic in the client. |

For more information on how YugabyteDB handles failures and its impact during transaction processing, refer to [Impact of failures](../../../architecture/transactions/distributed-txns/#impact-of-failures).

## Prerequisites

1. Follow the [setup instructions](../../#set-up-yugabytedb-universe) to start a local single region three-node universe. This creates a single region cluster with nodes in 3 different zones as shown in the following illustration:

    ![Local three node cluster](/images/explore/local_cluster_setup.svg)

1. Connect to your universe using [ysqlsh](../../../admin/ysqlsh/#starting-ysqlsh).

1. Create a tablespace to ensure that the leaders for the keys in the example transaction are located in node-2 as follows:

    ```plpgsql
    CREATE TABLESPACE txndemo_tablespace
    WITH (replica_placement='{"num_replicas": 3, "placement_blocks": [
        {"cloud":"aws","region":"us-east-2","zone":"us-east-2b","min_num_replicas":1,"leader_preference":1},
        {"cloud":"aws","region":"us-east-2","zone":"us-east-2a","min_num_replicas":1,"leader_preference":2},
        {"cloud":"aws","region":"us-east-2","zone":"us-east-2c","min_num_replicas":1,"leader_preference":3}
    ]}');
    ```

    {{< note title="Note" >}}
The examples use tablespaces so that the failure scenarios run in a deterministic manner in your cluster setup. YugabytedDB handles transaction failures in the same way either with or without tablespaces.
    {{< /note >}}

1. Create a table in the tablespace using the following command:

    ```plpgsql
    CREATE TABLE txndemo (
        k int,
        v int,
        PRIMARY KEY(k)
    ) TABLESPACE txndemo_tablespace;
    ```

    Because the leader preference has been set to node-2 (us-east-2b), all the leaders for the `txndemo` table are now in node-2.

    ![Leaders in node-2](/images/explore/transactions/cluster_with_preferred_leaders.svg)

1. Insert sample data using the following command:

    ```plpgsql
    INSERT INTO txndemo SELECT id,10 FROM generate_series(1,5) AS id;
    ```

1. Navigate to <http://127.0.0.1:7000/tablet-servers> to list the servers and see where the data is located.

    ![Leaders in node-2](/images/explore/transactions/leaders-node2.png)

    All the leaders(__3/3__) are in node-2, and this is the node that you stop during the following failure scenarios.

1. Check the value of the row at `k=1` using the following command:

    ```plpgsql
    SELECT * from txndemo where k=1;
    ```

    ```output
      k | v
    ----+----
      1 | 10
    (1 row)
    ```

    The row with `k=1` has the value of `v=10.

## Node failure just before a transaction executes a statement

During a transaction, when a row is modified or fetched, YugabyteDB sends the corresponding request to the node containing the row that is being modified or fetched.

In this example, you can see how a transaction completes when the node that is about to receive a [provisional write](../../../architecture/transactions/distributed-txns/#provisional-records) fails by taking down node-2, as that node has the row the transaction is about to modify.

The following diagram illustrates the high-level steps that ensure transactions succeed when a node fails before receiving the write.

![Failure of a node before write](/images/explore/transactions/failure_node_before_write.svg)

1. Connect to node-1 as follows:

    ```sh
    ./bin/ysqlsh -h 127.0.0.1
    ```

1. Start a transaction as follows:

    ```plpgsql
    BEGIN;
    ```

    ```output.sql
    BEGIN
    Time: 2.047 ms
    ```

    The transaction is started, but as yet no row has been modified. At this point, no provisional records have been sent to node-2.

1. From another terminal of your YugabyteDB home directory, stop node-2 as follows:

    ```sh
    ./bin/yugabyted stop --base_dir=/tmp/ybd2
    ```

1. Navigate to <http://127.0.0.1:7000/tablet-servers> to verify that node-2 is gone from the tablet list.

    ![Leaders in node-1](/images/explore/transactions/leaders-node1.png)

    Node-2 is `DEAD` and a new leader (node-1) has been elected for all the tablets that were in node-2.

1. Update the value of row `k=1` to `20` and commit the transaction as follows:

    ```plpgsql
    UPDATE txndemo set v=20 where k=1;
    COMMIT;
    ```

    ```output.sql
    UPDATE 1
    Time: 1728.246 ms (00:01.728)
    COMMIT
    Time: 2.964 ms
    ```

    The transaction succeeds even though node-2 failed before receiving the provisional write, and the value updates to `20`. The transaction succeeds because a new leader (node-1) is quickly elected after the failure of node-2.

1. Check the value of the row at `k=1` using the following command:

    ```plpgsql
    SELECT * from txndemo where k=1;
    ```

    ```output
      k | v
    ----+----
      1 | 20
    (1 row)
    ```

    The row with `k=1` has the new value of `v=20`, confirming the completion of the transaction.

1. From another terminal of your YugabyteDB home directory, restart node-2 using the following command:

    ```sh
    ./bin/yugabyted start --base_dir=/tmp/ybd2
    ```

## Node failure just after a transaction executes a statement

As mentioned in the preceding example, when a row is modified or fetched during a transaction, YugabyteDB sends the appropriate statements to the node with the row that is being modified or fetched. In this example, you can see how a transaction completes when the node that has just received a [provisional write](../../../architecture/transactions/distributed-txns/#provisional-records) fails.

The following diagram illustrates the high-level steps that ensure transactions succeed when a node fails after receiving a statement.

![Failure of a node after write](/images/explore/transactions/failure_node_after_write.svg)

1. If not already connected, connect to `127.0.0.1` using the following ysqlsh command:

    ```sh
    ./bin/ysqlsh -h 127.0.0.1
    ```

1. Start a transaction to update the value of row `k=1` to `30` using the following commands:

    ```plpgsql
    BEGIN;
    ```

    ```output
    BEGIN
    Time: 2.047 ms
    ```

    ```plpgsql
    UPDATE txndemo set v=30 where k=1;
    ```

    ```output
    UPDATE 1
    Time: 51.513 ms
    ```

    The update succeeds. This means that the updated row with value `v=30` has been sent to node-2, but not yet been committed.

1. From another terminal of your YugabyteDB home directory, stop node-2, as this is the node that has received the modified row.

    ```sh
    ./bin/yugabyted stop --base_dir=/tmp/ybd2
    ```

1. Navigate to <http://127.0.0.1:7000/tablet-servers> to verify that node-2 is gone from the server list and that a new leader has been elected for the row with `k=1`.

    ![Leaders in node-1](/images/explore/transactions/leaders-node1.png)

    Node-2 is `DEAD` and a new leader (node-1) has been elected for all the tablets that were in node-2.

1. Commit the transaction as follows:

    ```plpgsql
    COMMIT;
    ```

    ```output.sql
    COMMIT
    Time: 6.243 ms
    ```

    The transaction succeeds even though node-2 failed after receiving the provisional write, and the row value updates to `30`. This is because the provisional writes were replicated to the follower tablets and when the leader failed, the newly elected leader already had the provisional writes, which enabled the transaction to continue without interruption.

1. Check the value of the row at `k=1` using the following command:

    ```plpgsql
    SELECT * from txndemo where k=1;
    ```

    ```output
      k | v
    ----+----
      1 | 30
    (1 row)
    ```

    The row with `k=1` has the new value of `v=30`, confirming the completion of the transaction.

1. From another terminal of your YugabyteDB home directory, restart node-2 using the following command.

    ```sh
    ./bin/yugabyted start --base_dir=/tmp/ybd2
    ```

## Failure of the node to which a client has connected

The node to which a client connects acts as the manager for the transaction. The transaction manager coordinates the flow of transactions and maintains the correlation between the client and the transaction-id (a unique identifier for each transaction). YugabyteDB is inherently resilient to node failures as demonstrated in the preceding two scenarios.

In this example, you can see how a transaction aborts when the transaction manager fails. For more details on the role of the transaction manager, see [Transactional I/O](../../../architecture/transactions/transactional-io-path/#client-requests-transaction).

The following diagram illustrates the high-level steps that result in transactions aborting when the node that the client has connected to fails.

![Failure of a node the client is connected to](/images/explore/transactions/failure_client_connected_node.svg)

For this case, you can connect to any node in the cluster; `127.0.0.1` has been chosen in this example.

1. If not already connected, connect to node-1 as follows:

    ```sh
    ./bin/ysqlsh -h 127.0.0.1
    ```

1. Start a transaction to update the value of row `k=1` to `40` as follows:

    ```plpgsql
    BEGIN;
    ```

    ```output
    BEGIN
    Time: 2.047 ms
    ```

    ```plpgsql
    UPDATE txndemo set v=40 where k=1;
    ```

    ```output
    UPDATE 1
    Time: 50.624 ms
    ```

1. From another terminal of your YugabyteDB home directory, stop node-1 (the node that you have connected to) as follows:

    ```sh
    ./bin/yugabyted stop --base_dir=/tmp/ybd1
    ```

1. Commit the transaction as follows:

    ```plpgsql
    COMMIT;
    ```

    Note that the client receives an error response from the server similar to the following:

    ```output
    FATAL:  57P01: terminating connection due to unexpected postmaster exit
    server closed the connection unexpectedly
      This probably means the server terminated abnormally
      before or while processing the request.
    The connection to the server was lost. Attempting reset: Failed.
    Time: 2.499 ms
    ```

    The transaction failed with an error code `57P01`, which is a [standard PostgreSQL error code](https://www.postgresql.org/docs/11/errcodes-appendix.html) to indicate that the database server was restarted or your connection killed by the administrator. In such cases, the application should be programmed so that it automatically reconnects to the database when the connection fails.

1. From another terminal of your YugabyteDB home directory, connect to a different node and check the value as follows:

    ```sh
    ./bin/ysqlsh -h 127.0.0.2
    ```

    ```plpgsql
    SELECT * from txndemo where k=1;
    ```

    ```output
      k | v
    ----+----
      1 | 30
    (1 row)
    ```

    The transaction fails; the row does not get the intended value of `40`, and still retains the old value of `30`. When the transaction manager fails before a commit happens, the transaction is lost. At this point, it's the application's responsibility to restart the transaction.

{{% explore-cleanup-local %}}

## Learn More

- [Impact of Failures on a Transaction](../../../architecture/transactions/distributed-txns/#impact-of-failures) - Understand how failures impact the flow of transaction
- [Transactions Architecture](../../../architecture/transactions/transactions-overview/) - Understand how transactions are implemented in YugabyteDB.
- [Distributed transaction](../../transactions/distributed-transactions-ysql/) - Try out examples to understand different options associated with distributed transactions.

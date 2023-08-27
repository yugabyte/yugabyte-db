---
title: xCluster replication (2+ regions) in YugabyteDB
headerTitle: xCluster (2+ regions)
linkTitle: xCluster (2+ regions)
description: Multi-region deployment using asynchronous replication across two or more data centers.
headContent: Unidirectional (master-follower) and bidirectional (multi-master) replication
menu:
  stable:
    identifier: explore-multi-region-deployments-async-replication-1-ysql
    parent: explore-multi-region-deployments
    weight: 720
type: docs
---

By default, YugabyteDB provides synchronous replication and strong consistency across geo-distributed data centers. However, many use cases do not require synchronous replication or justify the additional complexity and operation costs associated with managing three or more data centers. A cross-cluster (xCluster) deployment provides asynchronous replication across two data centers or cloud regions.

This exercise simulates a geo-distributed two-data-center (2DC) deployment using two local YugabyteDB clusters, one representing "Data Center - East" and another representing "Data Center - West".

For more information, see the following:

- [xCluster replication architecture](../../../architecture/docdb-replication/async-replication/)
- [xCluster replication commands](../../../admin/yb-admin/#xcluster-replication-commands)
- [Change data capture (CDC)](../../../architecture/docdb-replication/change-data-capture/)

## Create two data centers

Create the two data centers as follows:

1. Create and start the local cluster that simulates "Data Center - East" by running the following command from your YugabyteDB home directory:

    ```sh
    ./bin/yugabyted start \
                    --advertise_address=127.0.0.1 \
                    --base_dir=/tmp/ybd1
    ```

    This starts a single-node local cluster using the IP address of `127.0.0.1` and creates `/tmp/ybd1` as the base directory.

1. If you are running on macOS and haven't created any loopback addresses, configure one as follows:

    ```sh
    sudo ifconfig lo0 alias 127.0.0.2
    ```

1. Create and start the second local cluster that simulates "Data Center - West" by running the following command:

    ```sh
    ./bin/yugabyted start \
                    --advertise_address=127.0.0.2 \
                    --base_dir=/tmp/ybd2
    ```

    This starts a single-node cluster using IP address of `127.0.0.2` and creates `/tmp/ybd2` as the base directory.

## Create tables

In the default `yugabyte` database, you can create the table `users` on the "Data Center - East" cluster:

1. Open `ysqlsh` by specifying the host IP address of `127.0.0.1`, as follows:

    ```sh
    ./bin/ysqlsh -h 127.0.0.1
    ```

1. Create the table `users`, as follows:

    ```sql
    CREATE TABLE users (
        email varchar PRIMARY KEY,
        username varchar
        );
    ```

Create an identical table on your second cluster:

1. Open `ysqlsh` for "Data Center - West" by specifying the host IP address of `127.0.0.2`, as follows:

    ```sh
    ./bin/ysqlsh -h 127.0.0.2
    ```

1. Create the table `users`, as follows

    ```sql
    CREATE TABLE users (
      email varchar(35) PRIMARY KEY,
      username varchar(20)
      );
    ```

Having two identical tables on your clusters allows you to set up xCluster replication across two data centers.

## Configure unidirectional replication

To configure "Data Center - West" to be the target of data changes from the "Data Center - East" cluster, you need to use the [yb-admin](../../../admin/yb-admin/) utility [setup_universe_replication](../../../admin/yb-admin/#xcluster-replication-commands) command. The syntax is as follows:

```sh.output
yb-admin -master_addresses <target-master-addresses> \
    setup_universe_replication <source-universe_uuid> \
    <source_master_addresses> <source-table-ids>
```

- *target-master-addresses*: a comma-separated list of the target YB-Master servers. For this simulation, you have one YB-Master server for the target, 127.0.0.2:7100.
- *source-universe-uuid*: a unique identifier for the source cluster. Look up the UUID in the source YB-Master UI (<http://127.0.0.1:7000>).
- *source-master-addresses*: a comma-separated list of the source YB-Master servers. For this simulation, you have one YB-Master server for the source, 127.0.0.1:7100.
- *source-table-ids*: A comma-separated list of table UUIDs. For this simulation, the `users` table; look up the UUID in the YB-Master UI (<http://127.0.0.1:7000/tables>).

Based on actual values you obtained from the YB-Master UI, run the yb-admin `setup_universe_replication` command from your YugabyteDB home directory similar to the one shown in the following example:

```sh
./bin/yb-admin -master_addresses 127.0.0.2:7100 \
    setup_universe_replication 7acd6399-657d-42dc-a90a-646869898c2d \
    127.0.0.1:7100 000033e8000030008000000000004000
```

- *target-master-addresses*: `127.0.0.2:7100`
- *source-universe-uuid*: `7acd6399-657d-42dc-a90a-646869898c2d`
- *source-master-addresses*: `127.0.0.1:7100`
- *source-table-ids*: `000033e8000030008000000000004000`

You should see the following message:

```output
Replication setup successfully
```

## Verify unidirectional replication

To check replication, you can add data to the `users` table on one cluster and see that data appear in the `users` table on your second cluster.

1. Use the following commands to add data to the "Data Center - East" cluster, making sure you are pointing to the new source host:

    ```sh
    ./bin/ysqlsh -h 127.0.0.1
    ```

    ```sql
    INSERT INTO users(email, username) VALUES ('hector@example.com', 'hector'), ('steve@example.com', 'steve');
    ```

1. On the target "Data Center - West" cluster, run the following commands to see that data has been replicated between clusters:

    ```sh
    ./bin/ysqlsh -h 127.0.0.2
    ```

    ```sql
    SELECT * FROM users;
    ```

    Expect the following output:

    ```output
            email         | username
    ---------------------+----------
      hector@example.com  | hector
      steve@example.com   | steve
    (2 rows)
    ```

## Configure bidirectional replication

Bidirectional xCluster replication lets you insert data into the same table on either of the clusters and have the data changes added to the other cluster.

To configure bidirectional replication for the same table, you run the `yb-admin` `setup_universe_replication` command to make the "Data Center - East" cluster the target of the "Data Center - West" cluster. This time, the target is 127.0.0.1:7100, and the source is 127.0.0.2:7100.

Look up the source UUID in the source YB-Master UI (<http://127.0.0.2:7000>), and source table UUID at <http://127.0.0.2:7000/tables>.

Based on actual values you obtained from the YB-Master UI, run the yb-admin `setup_universe_replication` command similar to the one shown in the following example:

```sh
./bin/yb-admin -master_addresses 127.0.0.1:7100 \
    setup_universe_replication 0a315687-e9bd-430f-b6f4-ac831193a394 \
    127.0.0.2:7100 000030a9000030008000000000004000
```

- *target-master-addresses*: `127.0.0.1:7100`
- *source-universe-uuid*: `0a315687-e9bd-430f-b6f4-ac831193a394`
- *source-master-addresses*: `127.0.0.2:7100`
- *source-table-ids*: `000030a9000030008000000000004000`

You should see the following message:

```output
Replication setup successfully
```

## Verify bidirectional replication

When the bidirectional replication has been configured, you can add data to the `users` table on the "Data Center - West" cluster and see the data appear in the `users` table on "Data Center - East" cluster.

1. Use the following commands to add data to the "Data Center - West" cluster, making sure you are pointing to the new source host:

    ```sh
    ./bin/ysqlsh -h 127.0.0.2
    ```

    ```sql
    INSERT INTO users(email, username) VALUES ('neha@example.com', 'neha'), ('mikhail@example.com', 'mikhail');
    ```

2. On the new target cluster, run the following commands to see that data has been replicated between clusters:

    ```sh
    ./bin/ysqlsh -h 127.0.0.1
    ```

    ```sql
    SELECT * FROM users;
     ```

    You should see the following output:

    ```output
            email         | username
    ---------------------+----------
      hector@example.com  | hector
      steve@example.com   | steve
      neha@example.com    | neha
      mikhail@example.com | mikhail
    (4 rows)
    ```

## Add tables

You can add more tables to an existing replication using the yb-admin command `alter_universe_replication` `add_table`:

```sh.output
yb-admin -master_addresses <target-master-addresses> \
        alter_universe_replication <source-universe_uuid> \
        add_table <source-table-ids>
```

The following is an example command:

```sh
./bin/yb-admin -master_addresses 127.0.0.2:7100 \
    alter_universe_replication 7acd6399-657d-42dc-a90a-646869898c2d \
    add_table 000030a9000030008000000000004000
```

For details, see [alter_universe_replication](../../../admin/yb-admin/#alter-universe-replication).

{{% explore-cleanup-local %}}

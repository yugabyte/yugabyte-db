---
title: xCluster replication (2+ regions) in YCQL
headerTitle: xCluster (2+ regions)
linkTitle: xCluster (2+ regions)
description: Multi-region deployment using asynchronous replication across two or more data centers in YCQL.
headContent: Unidirectional (master-follower) and bidirectional (multi-master) replication
menu:
  v2.16:
    identifier: explore-multi-region-deployments-async-replication-2-ycql
    parent: explore-multi-region-deployments
    weight: 720
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../asynchronous-replication-ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../asynchronous-replication-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

By default, YugabyteDB provides synchronous replication and strong consistency across geo-distributed data centers. However, many use cases do not require synchronous replication or justify the additional complexity and operational costs associated with managing three or more data centers. A cross-cluster (xCluster) deployment provides asynchronous replication across two data centers or cloud regions.

This document simulates a geo-distributed two-data-center (2DC) deployment using two local YugabyteDB clusters, one representing "Data Center - East" and the other representing "Data Center - West". Examples are based on the default database `yugabyte` and the default user `yugabyte`.

For more information, see the following:

- [xCluster replication architecture](../../../architecture/docdb-replication/async-replication/)
- [xCluster replication commands](../../../admin/yb-admin/#xcluster-replication-commands)
- [Change data capture (CDC)](../../../architecture/docdb-replication/change-data-capture/)
- [yugabyted](../../../reference/configuration/yugabyted/) 
- [yb-admin](../../../admin/yb-admin/)

## Create two data centers

Create and start your local clusters that simulate "Data Center - East" and "Data Center - West" by following instructions provided in [Create two data centers](../asynchronous-replication-ysql/#create-two-data-centers).

## Create keyspace and tables

Create the keyspace `customers` and table `users` on the "Data Center - East" cluster:

1. Open `ycqlsh` by specifying the host IP address of `127.0.0.1`, as follows:

   ```sh
   ./bin/ycqlsh 127.0.0.1
   ```

1. Create the `customers` keyspace by executing the following:

   ```sql
   CREATE KEYSPACE customers;
   ```

1. Enter the keyspace by executing the following:

   ```sql
   USE customers;
   ```

1. Create the `users` table by executing the following:

   ``` sql
   CREATE TABLE users ( email varchar PRIMARY KEY, username varchar );
   ```

Create the identical database table on the second cluster:

1. Open `ycqlsh` for "Data Center - West" by specifying the host IP address of `127.0.0.2`, as follows:

   ```sh
   ./bin/ycqlsh 127.0.0.2
   ```

1. Create the `customers` keyspace by executing the following:

   ```sql
   CREATE KEYSPACE customers;
   ```

1. Enter the keyspace by executing the following:

   ```sql
   USE customers;
   ```

1. Create the `users` table by executing the following:

   ```sql
   CREATE TABLE users ( email varchar PRIMARY KEY, username varchar );
   ```

Having two identical tables on your clusters allows you to set up xCluster replication across two data centers.

## Configure unidirectional replication

To configure "Data Center - West" to be the target of data changes from the "Data Center - East" cluster, follow instructions provided in [Configure unidirectional replication](../asynchronous-replication-ysql/#configure-unidirectional-replication).

## Verify unidirectional replication

To check replication, you can add data to the `users` table on one cluster and see that data appear in the `users` table on your second cluster.

1. Use the following commands to add data to the "Data Center - East" cluster, making sure you are pointing to the new source host:

   ```sh
   ./bin/ycqlsh 127.0.0.1
   ```

     ```sql
   ycqlsh:customers> INSERT INTO users(email, username) VALUES ('hector@example.com', 'hector');
   ycqlsh:customers> INSERT INTO users(email, username) VALUES ('steve@example.com', 'steve');
     ```

1. On the target "Data Center - West" cluster, run the following commands to see that data has been replicated between clusters:

   ```sh
   ./bin/ycqlsh 127.0.0.2
   ```

     ```sql
   ycqlsh:customers> SELECT * FROM users;
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

To configure bidirectional replication for the same table, follow instructions provided in [Configure bidirectional replication](../asynchronous-replication-ysql/#configure-bidirectional-replication).

## Verify bidirectional replication

When the bidirectional replication has been configured, you can add data to the `users` table on the "Data Center - West" cluster and see the data appear in the `users` table on "Data Center - East" cluster.

1. Use the following commands to add data to the "Data Center - West" cluster, making sure you are pointing to the new source host:

   ```sh
   ./bin/ycqlsh 127.0.0.2
   ```

     ```sql
   ycqlsh:customers> INSERT INTO users(email, username) VALUES ('neha@example.com', 'neha');
   ycqlsh:customers> INSERT INTO users(email, username) VALUES ('mikhail@example.com', 'mikhail');
     ```

2. On the new target cluster, run the following commands to see that data has been replicated between clusters:

   ```sh
   ./bin/ycqlsh 127.0.0.1
   ```

     ```sql
   ycqlsh:customers> SELECT * FROM users;
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

You can add more tables to an existing replication by following instructions provided in [Add tables](../asynchronous-replication-ysql/#add-tables).

## Clean up

You can choose to destroy and remove the clusters along with their associated directories by following instructions provided in [Clean up](../asynchronous-replication-ysql/#clean-up).

---
title: xCluster replication (2+ regions) in YSQL
headerTitle: xCluster (2+ regions)
linkTitle: xCluster (2+ regions)
description: Multi-region deployment using asynchronous replication across two or more data centers in YSQL.
headContent: Unidirectional (master-follower) and bidirectional (multi-master) replication
menu:
  v2.16:
    identifier: explore-multi-region-deployments-async-replication-1-ysql
    parent: explore-multi-region-deployments
    weight: 720
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../asynchronous-replication-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../asynchronous-replication-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

By default, YugabyteDB provides synchronous replication and strong consistency across geo-distributed data centers. However, many use cases do not require synchronous replication or justify the additional complexity and operation costs associated with managing three or more data centers. A cross-cluster (xCluster) deployment provides asynchronous replication across two data centers or cloud regions.

This document simulates a geo-distributed two-data-center deployment using two local YugabyteDB clusters, one representing "Data Center - East" and another representing "Data Center - West". Examples are based on the default database `yugabyte` and the default user `yugabyte`.

For more information, see the following:

- [xCluster replication architecture](../../../architecture/docdb-replication/async-replication/)
- [xCluster replication commands](../../../admin/yb-admin/#xcluster-replication-commands)
- [Change data capture (CDC)](../../../architecture/docdb-replication/change-data-capture/)
- [yugabyted](../../../reference/configuration/yugabyted/) 
- [yb-admin](../../../admin/yb-admin/)

## Create two data centers

1. You can create and start your first local cluster that simulates "Data Center - East" by running the following `yugabyted start` command from your YugabyteDB home directory:

   ```sh
   ./bin/yugabyted start --base_dir=datacenter-east --listen=127.0.0.1
   ```
   
   The preceding command starts a one-node local cluster using the IP address of `127.0.0.1` and creates `datacenter-east` as the base directory. Expect to see an output similar to the following:

   

   ```output
   Starting yugabyted...
   ✅ System checks
   
   +--------------------------------------------------------------------------------------------------+
   |                                            yugabyted                                             |
   +--------------------------------------------------------------------------------------------------+
   | Status              : Running. Leader Master is present                                          |
   | Web console         : http://127.0.0.1:7000                                                      |
   | JDBC                : jdbc:postgresql://127.0.0.1:5433/yugabyte?user=yugabyte&password=yugabyte  |
   | YSQL                : bin/ysqlsh   -U yugabyte -d yugabyte                                       |
   | YCQL                : bin/ycqlsh   -u cassandra                                                  |
   | Data Dir            : /Users/myuser/yugabyte-2.7.2.0/datacenter-east/data                        |
   | Log Dir             : /Users/myuser/yugabyte-2.7.2.0/datacenter-east/logs                        |
   | Universe UUID       : 4fb04760-4b6d-46a7-83cf-a89b2a056579                                       |
   +--------------------------------------------------------------------------------------------------+
   🚀 yugabyted started successfully! To load a sample dataset, try 'yugabyted demo'.
   🎉 Join us on Slack at https://www.yugabyte.com/slack
   👕 Claim your free t-shirt at https://www.yugabyte.com/community-rewards/
   ```
   
1. Create and start your second local cluster that simulates "Data Center = West" by running the following `yugabyted start` command from your YugabyteDB home directory:

     ```sh
   ./bin/yugabyted start --base_dir=datacenter-west --listen=127.0.0.2
   ```
   
    The preceding command starts a one-node cluster using IP address of `127.0.0.2` and creates `datacenter-west` as the base directory. Expect to see an output similar to the following:

   

     ```output
   Starting yugabyted...
   ✅ System checks
   
   +--------------------------------------------------------------------------------------------------+
   |                                            yugabyted                                             |
   +--------------------------------------------------------------------------------------------------+
   | Status              : Running. Leader Master is present                                          |
   | Web console         : http://127.0.0.2:7000                                                      |
   | JDBC                : jdbc:postgresql://127.0.0.2:5433/yugabyte?user=yugabyte&password=yugabyte  |
   | YSQL                : bin/ysqlsh -h 127.0.0.2  -U yugabyte -d yugabyte                           |
   | YCQL                : bin/ycqlsh 127.0.0.2 9042 -u cassandra                                     |
   | Data Dir            : /Users/myuser/yugabyte-2.7.2.0/datacenter-west/data                        |
   | Log Dir             : /Users/myuser/yugabyte-2.7.2.0/datacenter-west/logs                        |
   | Universe UUID       : ad78f70c-0741-4c7e-b610-315d55d7f248                                       |
   +--------------------------------------------------------------------------------------------------+
   🚀 yugabyted started successfully! To load a sample dataset, try 'yugabyted demo'.
   🎉 Join us on Slack at https://www.yugabyte.com/slack
   👕 Claim your free t-shirt at https://www.yugabyte.com/community-rewards/
     ```


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

To configure "Data Center - West" to be the target of data changes from the "Data Center - East" cluster, you need to use the `yb-admin` `setup_universe_replication` command. Review the syntax and run the following command:

```sh
yb-admin -master_addresses <target-master-addresses> \
setup_universe_replication <source-universe_uuid> <source_master_addresses> <source-table-ids>
```

- *target-master-addresses*: a comma-separated list of the YB-Master servers. For this simulation, you have one YB-Master server for each cluster (typically, there are three).
- *source-universe-uuid*: a unique identifier for the source cluster. The UUID can be found in the YB-Master UI (`<yb-master-ip>:7000`).
- *source-table-ids*: A comma-separated list of `table_id` values. The generated UUIDs can be found in the YB-Master UI (`<yb-master-ip>:7000`/tables).

Based on your actual values (which you obtained from the YB-Master UI page at `yb-master-ip>:7000`), run the `yb-admin` `setup_universe_replication` command similar to the one shown in the following example:

```sh
./bin/yb-admin -master_addresses 127.0.0.2:7100 \
setup_universe_replication 7acd6399-657d-42dc-a90a-646869898c2d 127.0.0.1:7100 000030a9000030008000000000004000
```

- *target-master-addresses*: `127.0.0.2:7100`
- *source-universe-uuid*: `7acd6399-657d-42dc-a90a-646869898c2d`
- *source-master-addresses*: `127.0.0.1:7100`
- *source-table-ids*: `000030a9000030008000000000004000`

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

To configure bidirectional replication for the same table, you need to run the following `yb-admin` `setup_universe_replication` command to make the "Data Center - East" cluster the target of the "Data Center - West" cluster:

```sh
./bin/yb-admin -master_addresses 127.0.0.1:7100 \
setup_universe_replication 0a315687-e9bd-430f-b6f4-ac831193a394 127.0.0.2:7100 000030a9000030008000000000004000
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

You can add more tables to an existing replication using the `yb-admin` command `alter_universe_replication` `add_table`:

```sh
yb-admin -master_addresses <target-master-addresses> alter_universe_replication <source-universe_uuid> add_table <source-table-ids>
```

The following is an example command:

```sh
./bin/yb-admin -master_addresses 127.0.0.2:7100 \
alter_universe_replication 7acd6399-657d-42dc-a90a-646869898c2d add_table 000030a9000030008000000000004000
```

For details, see [alter_universe_replication](../../../admin/yb-admin/#alter-universe-replication).

## Clean up


You can choose to destroy and remove the clusters along with their associated directories.

To stop the simulated data centers, you execute the `yugabyted stop` commands using the `--base_dir` option to specify the cluster.

You can stop "Data Center - East" as follows:

```sh
./bin/yugabyted stop --base_dir=datacenter-east
```

To destroy a simulated data center and remove its associated directory, use the `yugabyted destroy` command with the `--base_dir` option to specify the cluster.

You can destroy and remove the "Data Center - West" as follows:

```sh
./bin/yugabyted destroy --base_dir=datacenter-west
```


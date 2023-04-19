---
title: xCluster replication (2+ regions) in YCQL
headerTitle: xCluster replication (2+ regions) in YCQL
linkTitle: xCluster replication (2+ regions)
description: Multi-region deployment using asynchronous replication across two or more data centers in YCQL.
menu:
  v2.14:
    name: xCluster replication (2+ regions)
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

By default, YugabyteDB provides synchronous replication and strong consistency across geo-distributed data centers (also known as universes). But sometimes xCluster replication will meet your need for disaster recovery, auditing and compliance, and other applications. For more information, see [Two data center (2DC) deployments](../../../architecture/docdb-replication/async-replication/) in the Architecture section.

This tutorial simulates a geo-distributed two data center (2DC) deployment using two local YugabyteDB clusters, one representing "Data Center - East" and the other representing "Data Center - West." You can explore unidirectional (master-follower) xCluster replication and bidirectional (multi-master) xCluster replication using the [yugabyted](../../../reference/configuration/yugabyted/) and [yb-admin](../../../admin/yb-admin/) utilities.

## 1. Create two "data centers"

Create and start your first local cluster that will simulate "Data Center - East" by running the following `yugabyted start` command from your YugabyteDB home directory:

```sh
$ ./bin/yugabyted start \
                  --base_dir=datacenter-east \
                  --listen=127.0.0.1
```

This will start up a one-node local cluster using the IP address of `127.0.0.1` and create `datacenter-east` as the base directory. Upon starting, you should see an output similar to the following:

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

Create and start your second local cluster that will simulate "Data Center = West" by running the following `yugabyted start` command from your YugabyteDB home directory:

```sh
$ ./bin/yugabyted start \
                  --base_dir=datacenter-west \
                  --listen=127.0.0.2
```

This will start up a one-node cluster using IP address of `127.0.0.2` and create `datacenter-west` as the base directory. Upon starting, you should see an output similar to the following:

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

## 2. Create keyspace and tables

Create the keyspace `customers` and table `users` on the "Data Center - East" cluster.

Open `ycqlsh` specifying the host IP address of `127.0.0.1` by executing the following command:

```sh
$ ./bin/ycqlsh 127.0.0.1
```

Create the `customers` keyspace by executing the following:

```sql
CREATE KEYSPACE customers;
```

Enter the keyspace by executing the following:

```sql
USE customers;
```

Create the `users` table by executing the following:

``` sql
CREATE TABLE users ( email varchar PRIMARY KEY, username varchar );
```

Now create the identical database table on the second cluster.

Open `ycqlsh` for "Data Center - West" by specifying the host IP address of `127.0.0.2`, as follows:

```sh
$ ./bin/ycqlsh 127.0.0.2
```

Create the `customers` keyspace by executing the following:

```sql
CREATE KEYSPACE customers;
```

Enter the keyspace by executing the following:

```sql
USE customers;
```

Create the `users` table by executing the following:

```sql
CREATE TABLE users ( email varchar PRIMARY KEY, username varchar );
```

You now have the identical database table on each of your clusters and can set up 2DC xCluster replication.

## 3. Configure unidirectional replication

To configure "Data Center - West" to be the target of data changes from the "Data Center - East" cluster, you need to use the `yb-admin` `setup_universe_replication` command. Review the syntax and then run the command:

```sh
yb-admin -master_addresses <target-master-addresses> \
setup_universe_replication <source-universe_uuid> <source_master_addresses> <source-table-ids>
```

- *target-master-addresses*: a comma-separated list of the YB-Master servers. For this simulation, you have one YB-Master server for each cluster (typically, there are three).
- *source-universe-uuid*: a unique identifier for the source cluster. The UUID can be found in the YB-Master UI (`<yb-master-ip>:7000`).
- *source-table-ids*: A comma-separated list of `table_id` values. The generated UUIDs can be found in the YB-Master UI (`<yb-master-ip>:7000`/tables).

Based on your actual values (which you got from the YB-Master UI page at `yb-master-ip>:7000`), run the `yb-admin` `setup_universe_replication` command, as per the following example:

```sh
$ ./bin/yb-admin -master_addresses 127.0.0.2:7100 \
setup_universe_replication 7acd6399-657d-42dc-a90a-646869898c2d 127.0.0.1:7100 000030a9000030008000000000004000
```

- *target-master-addresses*: `127.0.0.2:7100`
- *source-universe-uuid*: `7acd6399-657d-42dc-a90a-646869898c2d`
- *source-master-addresses*: `127.0.0.1:7100`
- *source-table-ids*: `000030a9000030008000000000004000`

You should see the following output:

```output
Replication setup successfully
```

## 4. Verify unidirectional replication

Now that you've configured unidirectional replication, you can add data to the `users` table on the "Data Center - East" cluster and see the data appear in the `users` table on "Data Center - West" cluster.

To add data to the "Data Center - East" cluster, open `ycqlsh` by running the following commands, making sure you are pointing to the new source host:

```sh
$ ./bin/ycqlsh 127.0.0.1
```

```sql
ycqlsh:customers> INSERT INTO users(email, username) VALUES ('hector@example.com', 'hector');
ycqlsh:customers> INSERT INTO users(email, username) VALUES ('steve@example.com', 'steve');
```

On the target "Data Center - West" cluster, open `ycqlsh` and run the following commands to see that data has been replicated between clusters:

```sh
$ ./bin/ycqlsh 127.0.0.2
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
(2 rows)
```

## 5. Configure bidirectional replication (optional)

Bidirectional xCluster replication lets you insert data into the same table on either of the clusters and have the data changes added to the other cluster.

To configure bidirectional xCluster replication for the same table, you need to run the following `yb-admin` `setup_universe_replication` command to set up the "Data Center - East" cluster to be the target of the "Data Center - West" cluster:

```sh
$ ./bin/yb-admin -master_addresses 127.0.0.1:7100 \
setup_universe_replication 0a315687-e9bd-430f-b6f4-ac831193a394  127.0.0.2:7100 000030a9000030008000000000004000
```

- *target-master-addresses*: `127.0.0.1:7100`
- *source-universe-uuid*: `0a315687-e9bd-430f-b6f4-ac831193a394`
- *source-master-addresses*: `127.0.0.2:7100`
- *source-table-ids*: `000030a9000030008000000000004000`

You should see the following output:

```output
Replication setup successfully
```

## 6. Verify bidirectional replication (optional)

Now that you've configured bidirectional replication, you can add data to the `users` table on the "Data Center - West" cluster and see the data appear in the `users` table on "Data Center - East" cluster.

To add data to the "Data Center - West" cluster, open `ycqlsh` by running the following commands, making sure you are pointing to the new source host:

```sh
$ ./bin/ycqlsh 127.0.0.2
```

```plpgsql
ycqlsh:customers> INSERT INTO users(email, username) VALUES ('neha@example.com', 'neha');
ycqlsh:customers> INSERT INTO users(email, username) VALUES ('mikhail@example.com', 'mikhail');
```

On the new target cluster, open `ycqlsh` and run the following commands to see that data has been replicated between clusters:

```sh
$ ./bin/ycqlsh 127.0.0.1
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

## 7. Clean up

At this point, you've finished the tutorial. You can either stop and save your examples or destroy and remove the clusters and their associated directories.

To stop the simulated data centers, use the `yugabyted stop` commands using the `--base_dir` option to specify the cluster.

**Example - stopping "Data Center - East"**

```sh
$ ./bin/yugabyted stop \
                  --base_dir=datacenter-east
```

To destroy a simulated data center and remove its associated directory, use the `yugabyted destroy` command with the `--base_dir` option to specify the cluster.

**Example — destroying and removing the "Data Center - West"**

```sh
$ ./bin/yugabyted destroy \
                  --base_dir=datacenter-west
```

## What's next?

For more information, see the following:

- [xCluster replication](../../../architecture/docdb-replication/async-replication/)
- [Change data capture (CDC)](../../../architecture/docdb-replication/change-data-capture/)

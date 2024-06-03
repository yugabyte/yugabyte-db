---
title: Create a multi-zone universe using YugabyteDB Anywhere
headerTitle: Create a multi-zone universe
linkTitle: Multi-zone universe
description: Use YugabyteDB Anywhere to create a YugabyteDB universe that spans multiple availability zones.
menu:
  v2.16_yugabyte-platform:
    identifier: create-multi-zone-universe
    parent: create-deployments
    weight: 30
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../create-universe-multi-zone/" class="nav-link active">
      <i class="fa-solid fa-building" aria-hidden="true"></i>
Generic</a>
  </li>

  <li>
    <a href="../create-universe-multi-zone-kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>

<br>You can create a YugabyteDB universe using any cloud provider, except Kubernetes, in one geographic region across multiple availability zones.

## Prerequisites

Before you start creating a universe, ensure that you performed steps applicable to the cloud provider of your choice, as described in [Configure a cloud provider](/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/).

## Create a universe

If no universes have been created yet, the **Dashboard** does not display any.

Click **Create Universe** to create a universe and then enter your intent.

The **Provider**, **Regions**, and **Instance Type** fields are initialized based on the [configured cloud providers](../../configure-yugabyte-platform/set-up-cloud-provider/). When you provide the value in the **Nodes** field, the nodes are automatically placed across all the availability zones to guarantee the maximum availability.

To create a multi-zone universe using [Google Cloud provider (GCP)](../../configure-yugabyte-platform/set-up-cloud-provider/gcp/), perform the following:

- Enter a universe name (**helloworld1**).

- Enter the region (**Oregon**).

- Change the instance type (**n1-standard-8**).

- Accept default values for all of the remaining fields (replication factor = 3, number of nodes = 3), as per the following illustration:<br>

  ![Create Universe on GCP](/images/yp/create-uni-multi-zone-1.png)<br>

- Click **Create**.

## Examine the universe

The **Universes** view allows you to examine various aspects of the universe:

- **Overview** provides the information on the current YugabyteDB Anywhere version, the number of nodes included in the primary cluster, the cost associated with running the universe, the CPU and disk usage, the geographical location of the nodes, the operations per second and average latency, the number of different types of tables, as well as the health monitor.
- **Tables** provides details about YSQL, YCQL, and YEDIS tables included in the universe.
- **Nodes** provide details on nodes included in the universe and allows you to perform actions on a specific node (connect, stop, remove, display live and slow queries, download logs). You can also use **Nodes** to open the cloud provider's instances page. For example, in case of GCP, if you navigate to **Compute Engine > VM Instances** and search for instances that contain the name of your universe in the instances name, you should see a list of instances.
- **Metrics** displays graphs representing information on operations, latency, and other parameters for each type of node and server.
- **Queries** displays details about live and slow queries that you can filter by column and text.
- **Replication** provides information about any [xCluster replication](../../create-deployments/async-replication-platform/) in the universe.
- **Tasks** provides details about the state of tasks running on the universe, as well as the tasks that have run in the past against this universe.
- **Backups** displays information about scheduled backups, if any, and allows you to create, restore, and delete backups.
- **Health** displays the detailed performance status of the nodes and components involved in their operation. **Health** also allows you to pause health check alerts.

## Connect to a database node

Once the universe is ready, its **Overview** tab should appear similar to the following illustration:

![Multi-zone universe ready](/images/yp/multi-zone-universe-ready-1.png)

You connect to a database node as follows:

- Open the **Nodes** tab to find a list of the IP addresses of the available nodes that have been created and configured, as shown in the following illustration:

  ![Multi-zone universe nodes](/images/yp/multi-zone-universe-nodes-1.png)

- Determine the node to which you wish to connect and click the corresponding **Action > Connect**.

- Copy the SSH command displayed in the **Access your node** dialog shown in the following illustration:

  ![Multi-zone universe connect](/images/yp/multi-zone-universe-connect-2.png)

- Run the preceding command from the YugabyteDB Anywhere server, as follows:

  ```sh
  centos@yugaware-1:~$ sudo ssh -i /opt/yugabyte/yugaware/data/keys/109e95b5-bf08-4a8f-a7fb-2d2866865e15/yb-gcp-config-key.pem -ostricthostkeychecking=no -p 54422 yugabyte@10.150.1.56

  Are you sure you want to continue connecting (yes/no)? yes
  [centos@yb-dev-helloworld1-n1 ~]$
  ```

## Run workloads

YugabyteDB Anywhere includes a number of sample applications enclosed in Docker containers.

To access instructions on how to run sample applications, select your universe's **Overview** and then click **Actions > Run Sample Apps** to open the **Run Sample Apps** dialog shown in the following illustration:

![Multi-zone universe sample apps](/images/yp/multi-zone-universe-sample-apps-1.png)

<!--

You can run one of the key-value workloads against the YCQL API and the YEDIS API as follows:

- Install Java by executing the following command:

  ```sh
  sudo yum install java-1.8.0-openjdk.x86_64 -y
  ```

- Switch to the yugabyte user by executing the following command:

  ```sh
  sudo su - yugabyte
  ```

- Export the `YCQL_ENDPOINTS` environment variable, supplying the IP addresses for nodes in the cluster, as follows:

  - Navigate to the **Universes > Overview** tab and click **YCQL Endpoints** to open a new tab with a list of IP addresses, as shown in the following illustration:

    ![YCQL end points](/images/ee/multi-zone-universe-ycql-endpoints.png)

  - Click the **Export** icon for **YCQL Services** to trigger export into a shell variable on the database node **yb-dev-helloworld1-n1** to which you are connected. Remember to replace the following IP addresses with those displayed in the YugabyteDB Anywhere UI.

    ```sh
    $ export YCQL_ENDPOINTS="10.138.0.3:9042,10.138.0.4:9042,10.138.0.5:9042"
    ```

- Export the `YEDIS_ENDPOINTS` environment variable by repeating the preceding procedure and as per the following illustration and command:

  ![YCQL end points](/images/ee/multi-zone-universe-yedis-endpoints.png)

  ```sh
  $ export YEDIS_ENDPOINTS="10.138.0.3:6379,10.138.0.4:6379,10.138.0.5:6379"
  ```

### CassandraKeyValue workload

To start the CassandraKeyValue workload, execute the following command:

```sh
java -jar /home/yugabyte/tserver/java/yb-sample-apps.jar \
            --workload CassandraKeyValue \
            --nodes $YCQL_ENDPOINTS \
            --num_threads_write 2 \
            --num_threads_read 32 \
            --value_size 128 \
            --num_unique_keys 10000000 \
            --nouuid
```

The sample application produces output similar to the following and reports some statistics in the steady state:

```output
Created table: [CREATE TABLE IF NOT EXISTS CassandraKeyValue (k varchar, v blob, primary key (k));]
...
Read: 47388.10 ops/sec (0.67 ms/op), 816030 total ops  | Write: 1307.80 ops/sec (1.53 ms/op), 22900 total ops
Read: 47419.99 ops/sec (0.67 ms/op), 1053156 total ops | Write: 1303.85 ops/sec (1.53 ms/op), 29420 total ops
Read: 47220.98 ops/sec (0.68 ms/op), 1289285 total ops | Write: 1311.67 ops/sec (1.52 ms/op), 35979 total ops
```

-->

The **Metrics** tab of the universe allows you to see the metrics graphs, where server-side metrics tally with the client-side metrics reported by the load tester.

<!--

![YCQL Load Metrics](/images/ee/multi-zone-universe-ycql-load-metrics.png)

-->

You can also view metrics at a per-node level.

<!--

![YCQL Load Metrics Per Node](/images/ee/multi-zone-universe-ycql-load-metrics-per-node.png)

-->

You can stop the load tester as follows:

- Find the container by executing the following command:

  ```shell
  user@yugaware-1:~$ sudo docker container ls | grep "yugabytedb/yb-sample-apps"
  ```

  Expect an output similar to the following:

  ```output
  <container_id> yugabytedb/yb-sample-apps "/usr/bin/java -jar …" 17 seconds ago Up 16 seconds                                                                                                            jovial_morse
  ```

  For example, if the container ID is ac144a49d57d, you would see the following output:

  ```output
  ac144a49d57d yugabytedb/yb-sample-apps "/usr/bin/java -jar …" 17 seconds ago Up 16 seconds                                                                                                            jovial_morse
  ```

- Stop the container by executing the following command:

  ```shell
  user@yugaware-1:~$ sudo docker container stop <container_id>
  ```

  Expect the following output:

  ```output
  <container_id>
  ```

  For example, for a container with ID ac144a49d57d, you would need to execute the following command:

  ```shell
  user@yugaware-1:~$ sudo docker container stop ac144a49d57d
  ```

  You would see the following output:

  ```output
  ac144a49d57d
  ```

<!--

### RedisKeyValue workload

To start the RedisKeyValue workload, execute the following command.

```sh
$ java -jar /home/yugabyte/tserver/java/yb-sample-apps.jar \
            --workload RedisKeyValue \
            --nodes $YEDIS_ENDPOINTS \
            --num_threads_write 2 \
            --num_threads_read 32 \
            --value_size 128 \
            --num_unique_keys 10000000 \
            --nouuid
```

The sample application produces output similar to the following and reports some statistics in the steady state:

```output
Read: 50069.15 ops/sec (0.64 ms/op), 657550 total ops  | Write: 1470.87 ops/sec (1.36 ms/op), 18849 total ops
Read: 50209.09 ops/sec (0.64 ms/op), 908653 total ops  | Write: 1454.87 ops/sec (1.37 ms/op), 26125 total ops
Read: 50016.18 ops/sec (0.64 ms/op), 1158794 total ops | Write: 1463.26 ops/sec (1.37 ms/op), 33443 total ops
```

If you open the **Metrics** tab of the universe, you should see the metrics graphs, as shown in the following illustration:

![YEDIS Load Metrics Per Node](/images/ee/multi-zone-universe-yedis-load-metrics.png)

Note that these server-side metrics tally with the client-side metrics reported by the load tester.

You should stop the sample application.

-->

## Examine data

You can connect to the YCQL service by executing the following command:

```sh
/home/yugabyte/tserver/bin/ycqlsh <ip_address_of_the_node>
```

You can view the table schema and the data, as follows:

```sql
ycqlsh> DESCRIBE ybdemo_keyspace.cassandrakeyvalue;

CREATE TABLE ybdemo_keyspace.cassandrakeyvalue (
  k text PRIMARY KEY,
  v blob
) WITH default_time_to_live = 0;
```

```sql
ycqlsh> SELECT * FROM ybdemo_keyspace.cassandrakeyvalue LIMIT 5;
```

```output
 k          | v
------------+-----------------------------------------
 key:101323 | 0x4276616c3a3130313332336be1dd6597e2...
 key:159968 | 0x4276616c3a3135393936381ed99587c08f...
  key:24879 | 0x4276616c3a3234383739054071b34c3fb6...
 key:294799 | 0x4276616c3a3239343739398b312748e80e...
 key:297045 | 0x4276616c3a32393730343525764eedee94...

(5 rows)
```

You can connect to the YEDIS service by executing the following command:

```sh
/home/yugabyte/tserver/bin/redis-cli -h <ip_address_of_the_node>
```

You can view the data by running the following commands:

```sh
10.138.0.4:6379> GET key:0
"Bval:0\x1b\x942\xea\xf0Q\xd1O\xdb\xf8...=V"
10.138.0.4:6379> GET key:1
"Bval:1\t\x1e\xa0=\xb66\x8b\x8eV\x82...,c"
10.138.0.4:6379>
```

---
title: Create a multi-zone universe using Yugabyte Platform
headerTitle: Create a multi-zone universe
linkTitle: Multi-zone universe
description: Use Yugabyte Platform to create a YugabyteDB universe that spans multiple availability zones.
menu:
  v2.6:
    identifier: create-multi-zone-universe
    parent: create-deployments
    weight: 20
isTocNested: true
showAsideToc: true
---

  <ul class="nav nav-tabs-alt nav-tabs-yb">

<li>
    <a href="/latest/yugabyte-platform/create-deployments/create-universe-multi-zone" class="nav-link active">
      <i class="fas fa-building" aria-hidden="true"></i>
Generic</a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/create-deployments/create-universe-multi-zone-kubernetes" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>

This section describes how to create a YugabyteDB universe using any cloud provider, except Kubernetes, in one geographic region across multiple availability zones.

## Prerequisites

Before you start creating a universe, ensure that you performed steps applicable to the cloud provider of your choice, as described in [Configure a cloud provider](/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/). 

## Create a universe

If no universes have been created yet, the Yugabyte Platform Dashboard looks similar to the following:

![Dashboard with No Universes](/images/ee/no-univ-dashboard.png)

Click **Create Universe** to create the universe, and then enter your intent.

The **Provider**, **Regions**, and **Instance Type** fields are initialized based on the [configured cloud providers](../../configure-yugabyte-platform/set-up-cloud-provider/). When you provide the value in the **Nodes** field, the nodes are automatically placed across all the availability zones to guarantee the maximum availability.

To create a multi-zone universe using [Google Cloud provider (GCP)](../../configure-yugabyte-platform/set-up-cloud-provider/gcp), perform the following:

- Enter a universe name (**helloworld1**).
- Enter the region (**Oregon**).
- Change the instance type (**n1-standard-8**).
- Accept default values for all of the remaining fields (replication factor = 3, number of nodes = 3).
- Click **Create**, as shown in the following illustration.

![Create Universe on GCP](/images/ee/create-univ-multi-zone.png)

The following illustration shows a newly-created niverse in Pending state:

![Dashboard with Pending Universe](/images/ee/pending-univ-dashboard.png)

## Examine the universe

The universe view consists of several tabs that provide different information about this universe.

### The Overview tab

The following illustration shows the **Overview** tab:

![Detail for a Pending Universe](/images/ee/pending-univ-detail.png)

### The Tasks tab 

The following illustration shows the **Tasks** tab that provides information about the state of tasks currently running on the universe, as well as the tasks that have run in the past against this universe:

![Tasks for a Pending Universe](/images/ee/pending-univ-tasks.png)

### The Nodes tab

The following illustration shows the **Nodes** tab that allows you to see a list of the underlying nodes for the universe:

![Nodes for a Pending Universe](/images/ee/pending-univ-nodes.png)

Note that in the preceding illustration, the cloud provider instances are still being created.

You can use this tab to open the cloud provider's instances page. For example, in case of GCP, if you navigate to **Compute Engine > VM Instances** and search for instances that contain **helloworld1** in their name, you should see a list of instances similar to the following illustration:

![Instances for a Pending Universe](/images/ee/multi-zone-universe-gcp-instances.png)

## Connect to a database node

Once the universe is ready, the **Overview** tab should appear similar to the following illustration:

![Multi-zone universe ready](/images/ee/multi-zone-universe-ready.png)

You connect to a database node as follows: 

- Open the **Nodes** tab to find a list of the IP addresses of the available nodes that have been created and configured.

- Click **Connect**, as shown in the following illustration:

  ![Multi-zone universe nodes](/images/ee/multi-zone-universe-nodes.png)

- Use the **Access Your Cluster** dialog to connect to the nodes, as shown in the following illustration:

  ![Multi-zone universe nodes](/images/ee/multi-zone-universe-nodes-connect.png)

For example, to connect to the first node called **yb-dev-helloworld1-n1**, copy the first command displayed in the **Access Your Cluster** dialog, and then run it from the Yugabyte Platform server, as follows:

```
centos@yugaware-1:~$ sudo ssh -i /opt/yugabyte/yugaware/data/keys/b933ff7a-be8a-429a-acc1-145882d90dc0/yb-dev-google-compute-key.pem centos@10.138.0.4

Are you sure you want to continue connecting (yes/no)? yes
[centos@yb-dev-helloworld1-n1 ~]$
```

## Run workloads

Yugabyte Platform includes a number of sample applications. You can run one of the key-value workloads against the YCQL API and the YEDIS API as follows:

- Install Java by executing the following command:

```sh
$ sudo yum install java-1.8.0-openjdk.x86_64 -y
```

- Switch to the yugabyte user by executing the following command:

```sh
$ sudo su - yugabyte
```

- Export the `YCQL_ENDPOINTS` environment variable, supplying the IP addresses for nodes in the cluster, as follows: 

  - Navigate to the **Universes > Overview** tab and click **YCQL Endpoints** to open a new tab with a list of IP addresses, as shown in the following illustration:

    ![YCQL end points](/images/ee/multi-zone-universe-ycql-endpoints.png)

  - Click the **Export** icon for **YCQL Services** to trigger export into a shell variable on the database node **yb-dev-helloworld1-n1** to which you are connected. Remember to replace the following IP addresses with those displayed in the Yugabyte Platform console.

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
$ java -jar /home/yugabyte/tserver/java/yb-sample-apps.jar \
            --workload CassandraKeyValue \
            --nodes $YCQL_ENDPOINTS \
            --num_threads_write 2 \
            --num_threads_read 32 \
            --value_size 128 \
            --num_unique_keys 10000000 \
            --nouuid
```

The sample application produces output similar to the following and reports some statistics in the steady state:

```
Created table: [CREATE TABLE IF NOT EXISTS CassandraKeyValue (k varchar, v blob, primary key (k));]
...
Read: 47388.10 ops/sec (0.67 ms/op), 816030 total ops  | Write: 1307.80 ops/sec (1.53 ms/op), 22900 total ops
Read: 47419.99 ops/sec (0.67 ms/op), 1053156 total ops | Write: 1303.85 ops/sec (1.53 ms/op), 29420 total ops
Read: 47220.98 ops/sec (0.68 ms/op), 1289285 total ops | Write: 1311.67 ops/sec (1.52 ms/op), 35979 total ops
```

If you open the **Metrics** tab of the universe, you should see the metrics graphs, as shown in the following illustration:

![YCQL Load Metrics](/images/ee/multi-zone-universe-ycql-load-metrics.png)

Note that these server-side metrics tally with the client-side metrics reported by the load tester.

You can also view metrics at a per-node level, as shown in the following illustration:

![YCQL Load Metrics Per Node](/images/ee/multi-zone-universe-ycql-load-metrics-per-node.png)

You should stop the load tester.

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

```
Read: 50069.15 ops/sec (0.64 ms/op), 657550 total ops  | Write: 1470.87 ops/sec (1.36 ms/op), 18849 total ops
Read: 50209.09 ops/sec (0.64 ms/op), 908653 total ops  | Write: 1454.87 ops/sec (1.37 ms/op), 26125 total ops
Read: 50016.18 ops/sec (0.64 ms/op), 1158794 total ops | Write: 1463.26 ops/sec (1.37 ms/op), 33443 total ops
```

If you open the **Metrics** tab of the universe, you should see the metrics graphs, as shown in the following illustration:

![YEDIS Load Metrics Per Node](/images/ee/multi-zone-universe-yedis-load-metrics.png)

Note that these server-side metrics tally with the client-side metrics reported by the load tester.

You shoudl stop the sample application.

## Examine data

You can connect to the YCQL service by executing the following command:

```
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

```
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

```
/home/yugabyte/tserver/bin/redis-cli -h <ip_address_of_the_node>
```

You can view the data by running the following commands:

```
10.138.0.4:6379> GET key:0
"Bval:0\x1b\x942\xea\xf0Q\xd1O\xdb\xf8...=V"
10.138.0.4:6379> GET key:1
"Bval:1\t\x1e\xa0=\xb66\x8b\x8eV\x82...,c"
10.138.0.4:6379>
```

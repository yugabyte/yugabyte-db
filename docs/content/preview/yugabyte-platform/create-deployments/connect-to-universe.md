---
title: Connect via client shells
linkTitle: Connect to a universe
description: Connect to YugabyteDB Anywhere universes from your desktop using a client shell
headcontent: Connect to database nodes from your desktop
menu:
  preview_yugabyte-platform:
    identifier: connect-to-universe
    parent: create-deployments
    weight: 80
type: docs
---

Connect to your YugabyteDB database from your desktop using the YugabyteDB [ysqlsh](../../../admin/ysqlsh/) and [ycqlsh](../../../admin/ycqlsh) client shells installed on your computer. Because YugabyteDB is compatible with PostgreSQL and Cassandra, you can also use [psql](https://www.postgresql.org/docs/current/app-psql.html) and third-party tools to connect.

You can connect to the database on a universe in the following ways:

- Using SSH to tunnel to one of the universe nodes, and running the ysqlsh or ycqlsh shell on the node.
- Downloading and running the client shells from your desktop.

## Connect to a universe node

The YugabyteDB shells are installed in the tserver/bin directory on each YB-TServer of a YugabyteDB universe.

To connect to a universe node, you should be signed on to the machine where YugabyteDB Anywhere is installed, or connected via SSH.

How you connect to your YugabyteDB Anywhere instance varies depending on the cloud provider hosting the instance. For example, for a GCP instance, you can SSH using a command similar to the following:

```sh
ssh -i secrets/yugabyte-platform-gcp-instance username@<platform_machine_external_ip>
```

To run a shell from a universe node, do the following:

1. In YugabyteDB Anywhere, navigate to your universe and select the **Nodes** tab.

  ![Multi-zone universe nodes](/images/yp/multi-zone-universe-nodes-1.png)

1. Determine the node to which you wish to connect and click **Actions > Connect**.

1. Copy the SSH command displayed in the **Access your node** dialog shown in the following illustration:

  ![Multi-zone universe connect](/images/yp/multi-zone-universe-connect-2.png)

1. From the machine where YugabyteDB Anywhere is installed, run the command you copied. For example:

  ```sh.output
  sudo ssh -i /opt/yugabyte/yugaware/data/keys/f000ad00-aafe-4a67-bd1f-34bdaf3bee00/yb-dev-yugabyte-google-provider_f033ad00-aafe-4a00-bd1f-34bdaf3bee00-key.pem -ostricthostkeychecking=no -p 22 yugabyte@<node_ip_address>

  Are you sure you want to continue connecting (yes/no)? yes
  [centos@yb-dev-helloworld1-n1 ~]$
  ```

1. Navigate to the tserver/bin directory:

  ```sh
  cd /home/yugabyte/tserver/bin
  ```

1. Run ysqlsh or ycqlsh as follows:

  ```sh
  ./ysqlsh -h <node_ip_address>
  ```

  ```sh
  ./ycqlsh <node_ip_address>
  ```

## Connect from your desktop

### Prerequisites

When connecting via a Yugabyte client shell, ensure you are running the latest versions of the shells (Yugabyte Client 2.6 or later). See [How do I connect to my cluster?](../../../faq/yugabytedb-managed-faq/#how-do-i-connect-to-my-cluster) in the FAQ for details.

YugabyteDB Anywhere universes have TLS/SSL (encryption in-transit) enabled. You need to download the certificate to your computer.

### Connect using a client shell

Use the ysqlsh and ycqlsh shells to connect to and interact with YuagbyteDB using the YSQL and YCQL APIs respectively. You can download and install the YugabyteDB client shells and connect to your database using the following steps for either YSQL or YCQL.

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#ysqlsh" class="nav-link active" id="ysqlsh-tab" data-toggle="tab" role="tab" aria-controls="ysqlsh" aria-selected="true">
      <i class="icon-postgres" aria-hidden="true"></i>
      ysqlsh
    </a>
  </li>
  <li>
    <a href="#ycqlsh" class="nav-link" id="ycqlsh-tab" data-toggle="tab" role="tab" aria-controls="ycqlsh" aria-selected="false">
      <i class="icon-cassandra" aria-hidden="true"></i>
      ycqlsh
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="ysqlsh" class="tab-pane fade show active" role="tabpanel" aria-labelledby="ysqlsh-tab">
  {{% includeMarkdown "connect/ysql.md" %}}
  </div>
  <div id="ycqlsh" class="tab-pane fade" role="tabpanel" aria-labelledby="ycqlsh-tab">
  {{% includeMarkdown "connect/ycql.md" %}}
  </div>
</div>

### Connect using psql

To connect using [psql](https://www.postgresql.org/docs/current/app-psql.html), first download the CA certificate for your cluster by clicking **Connect**, selecting **YugabyteDB Client Shell**, and clicking **Download CA Cert**. Then use the following connection string:

```sh
psql --host=<HOST_ADDRESS> --port=5433 \
--username=<DB USER> \
--dbname=yugabyte \
--set=sslmode=verify-full \
--set=sslrootcert=<ROOT_CERT_PATH>
```

Replace the following:

- `<HOST_ADDRESS>` with the value for host as shown under **Connection Parameters** on the **Settings > Infrastructure** tab for your cluster.
- `<DB USER>` with your database username.
- `yugabyte` with the database name, if you're connecting to a database other than the default (yugabyte).
- `<ROOT_CERT_PATH>` with the path to the root certificate on your computer.

For information on using other SSL modes, refer to [SSL modes in YSQL](../../cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql).

### Connect using third party clients

Because YugabyteDB is compatible with PostgreSQL and Cassandra, you can use third-party clients to connect to your YugabyteDB clusters in YugabyteDB Managed.

To connect, follow the client's configuration steps for PostgreSQL or Cassandra, and use the following values:

- **host** as shown under **Connection Parameters** on the **Settings > Infrastructure** tab for your cluster.
- **port** 5433 for YSQL, 9042 for YCQL.
- **database** name; the default YSQL database is yugabyte.
- **username** and **password** of a user with permissions for the database; the default user is admin.

Your client may also require the use of the cluster's certificate. Refer to [Download the cluster certificate](../../cloud-secure-clusters/cloud-authentication/#download-your-cluster-certificate).

For detailed steps for configuring popular third party tools, see [Third party tools](../../../tools/).

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
    export YCQL_ENDPOINTS="10.138.0.3:9042,10.138.0.4:9042,10.138.0.5:9042"
    ```

- Export the `YEDIS_ENDPOINTS` environment variable by repeating the preceding procedure and as per the following illustration and command:

  ![YCQL end points](/images/ee/multi-zone-universe-yedis-endpoints.png)

  ```sh
  export YEDIS_ENDPOINTS="10.138.0.3:6379,10.138.0.4:6379,10.138.0.5:6379"
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

## Related information

- [ysqlsh](../../../admin/ysqlsh/) — Overview of the command line interface (CLI), syntax, and commands.
- [YSQL API](../../../api/ysql/) — Reference for supported YSQL statements, data types, functions, and operators.
- [ycqlsh](../../../admin/ycqlsh/) — Overview of the command line interface (CLI), syntax, and commands.
- [YCQL API](../../../api/ycql/) — Reference for supported YCQL statements, data types, functions, and operators.

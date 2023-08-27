---
title: Explore YugabyteDB
headerTitle: Explore YugabyteDB
linkTitle: Explore
headcontent: Learn about YugabyteDB features, with examples
description: Explore the features of YugabyteDB on macOS, Linux, Docker, and Kubernetes.
image: /images/section_icons/index/explore.png
type: indexpage
showRightNav: true
---

The Explore documentation introduces you to YugabyteDB's features, often through examples.

Most examples demonstrating database features, such as API compatibility, can be run on a single-node universe either on your computer, using the free Sandbox universe (cluster) in YugabyteDB Managed, or using a universe created via YugabyteDB Anywhere. More advanced scenarios use a multi-node deployment. Refer to [Set up YugabyteDB universe](#set-up-yugabytedb-universe) for instructions on creating universes to run the examples.

## Overview

The following table describes the YugabyteDB features you can explore, along with the setup required to run the examples (single- or multi-node universe):

| Section | Purpose | [Universe&nbsp;setup](#set-up-yugabytedb-universe) |
| :--- | :--- | :--- |
| [SQL features](ysql-language-features/) | Learn about YugabyteDB's compatibility with PostgreSQL, including data types, queries, expressions, operators, extensions, and more. | Single-node<br/>local/cloud |
| [Going beyond SQL](ysql-language-features/going-beyond-sql/) | Learn about reducing read latency via follower reads and moving data closer to users using tablespaces. | Multi-node<br/>local |
| [Continuous availability](fault-tolerance/) | Learn how YugabyteDB achieves high availability when a node fails. | Multi-node<br/>local |
| [Horizontal scalability](linear-scalability/) | See how YugabyteDB handles loads while dynamically adding or removing nodes. | Multi-node<br/>local |
| [Transactions](transactions/) | Understand how distributed transactions and isolation levels work in YugabyteDB. | Single-node<br/>local/cloud |
| [Indexes and constraints](indexes-constraints/) | Explore indexes in YugabyteDB, including primary and foreign keys, secondary, unique, partial, and expression indexes, and more. | Single-node<br/>local/cloud |
| [JSON support](json-support/jsonb-ysql/) | YugabyteDB support for JSON is nearly identical to that in PostgreSQL - learn about JSON-specific functions and operators in YugabyteDB. | Single-node local/cloud |
| [Multi-region deployments](multi-region-deployments/) | Learn about the different multi-region topologies that you can deploy using YugabyteDB. | Multi-node<br/>local |
| [Query tuning](query-1-performance/) | Learn about the tools available to identify and optimize queries in YSQL. | Single-node<br/>local/cloud |
| [Cluster management](cluster-management/) | Learn how to roll back database changes to a specific point in time using point in time recovery. | Single-node<br/>local |
| [Change data capture](change-data-capture/) | Learn about YugabyteDB support for streaming data to Kafka. | N/A |
| [Security](security/security/) | Learn how to secure data in YugabyteDB, using authentication, authorization (RBAC), encryption, and more. | Single-node<br/>local/cloud |
| [Observability](observability/) | Export metrics into Prometheus and create dashboards using Grafana. | Multi-node<br/>local |

## Set up YugabyteDB universe

You can run examples using a universe set up on your local machine or in a cloud, assuming you have performed one of the following:

- [Installed](../quick-start/linux/) YugabyteDB.
- [Created an account](https://cloud.yugabyte.com/signup?utm_medium=direct&utm_source=docs&utm_campaign=Cloud_signup) in YugabyteDB Managed.
- [Installed](../yugabyte-platform/install-yugabyte-platform/) YugabyteDB Anywhere and [configured](../yugabyte-platform/configure-yugabyte-platform/) it to run in AWS.

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#local" class="nav-link active" id="local-tab" data-toggle="tab"
      role="tab" aria-controls="local" aria-selected="true">
      <img src="/icons/database.svg" alt="Server Icon">
      Local
    </a>
  </li>
  <li >
    <a href="#cloud" class="nav-link" id="cloud-tab" data-toggle="tab"
      role="tab" aria-controls="cloud" aria-selected="false">
      <img src="/icons/cloud.svg" alt="Cloud Icon">
      YugabyteDB Managed
    </a>
  </li>
  <li>
    <a href="#anywhere" class="nav-link" id="anywhere-tab" data-toggle="tab"
      role="tab" aria-controls="anywhere" aria-selected="false">
      <img src="/icons/server.svg" alt="Server Icon">
      YugabyteDB Anywhere
    </a>
  </li>
</ul>
<div class="tab-content">
  <div id="local" class="tab-pane fade show active" role="tabpanel" aria-labelledby="local-tab">

To run the examples, you need to create a single- or multi-node universe.

For testing and learning YugabyteDB, use the [yugabyted](../reference/configuration/yugabyted/) utility to create and manage universes.

The following instructions show how to _simulate_ a single- or multi-node universe on a single computer. To deploy an actual multi-zone universe using yugabyted, follow the instructions in [Create a multi-zone cluster](../reference/configuration/yugabyted/#create-a-multi-zone-cluster).

{{< tabpane text=true >}}

  {{% tab header="Single-node universe" lang="Single-node universe" %}}

If a local universe is currently running, first [destroy it](../reference/configuration/yugabyted/#destroy-a-local-cluster).

You can create a single-node local universe with a replication factor (RF) of 1 by running the following command:

```sh
./bin/yugabyted start --advertise_address=127.0.0.1
```

Or, if you are running macOS Monterey, use the following command:

```sh
./bin/yugabyted start --advertise_address=127.0.0.1 \
                      --master_webserver_port=9999
```

To check the status of a running single-node universe, run the following command:

```sh
./bin/yugabyted status
```

For more information, refer to [Quick Start](../quick-start/linux/#create-a-local-cluster).

  {{% /tab %}}

  {{% tab header="Multi-node universe" lang="Multi-node universe" %}}

If a local universe is currently running, first [destroy it](../reference/configuration/yugabyted/#destroy-a-local-cluster).

Start a local three-node universe with an RF of `3` by first creating a single node universe, as follows:

```sh
./bin/yugabyted start \
                --advertise_address=127.0.0.1 \
                --base_dir=/tmp/ybd1 \
                --cloud_location=aws.us-east-2.us-east-2a
```

On macOS, the additional nodes need loopback addresses configured, as follows:

```sh
sudo ifconfig lo0 alias 127.0.0.2
sudo ifconfig lo0 alias 127.0.0.3
```

Next, join two more nodes with the previous node. yugabyted automatically applies a replication factor of `3` when a third node is added, as follows:

```sh
./bin/yugabyted start \
                --advertise_address=127.0.0.2 \
                --base_dir=/tmp/ybd2 \
                --cloud_location=aws.us-east-2.us-east-2b \
                --join=127.0.0.1
```

```sh
./bin/yugabyted start \
                --advertise_address=127.0.0.3 \
                --base_dir=/tmp/ybd3 \
                --cloud_location=aws.us-east-2.us-east-2c \
                --join=127.0.0.1
```

After starting the yugabyted processes on all the nodes, configure the data placement constraint of the universe, as follows:

```sh
./bin/yugabyted configure data_placement --fault_tolerance=zone --base_dir=/tmp/ybd1
```

This command can be executed on any node where you already started YugabyteDB.

To check the status of a running multi-node universe, run the following command:

```sh
./bin/yugabyted status --base_dir=/tmp/ybd1
```

  {{% /tab %}}

{{< /tabpane >}}

**Connect to universes**

To run the examples in your universe, you use either the ysqlsh or ycqlsh CLI to interact with YugabyteDB via the YSQL or YCQL API.

You can start ysqlsh as follows:

```sh
./bin/ysqlsh
```

```output
ysqlsh (11.2-YB-2.0.0.0-b0)
Type "help" for help.

yugabyte=#
```

You can start ycqlsh as follows:

```sh
./bin/ycqlsh
```

```output
Connected to local cluster at 127.0.0.1:9042.
[ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
ycqlsh>
```

  </div>

  <div id="cloud" class="tab-pane fade" role="tabpanel" aria-labelledby="cloud-tab">

To run the examples in YugabyteDB Managed, create a single- or multi-node universe (which is referred to as cluster in YugabyteDB Managed).

{{< tabpane text=true >}}

  {{% tab header="Single-node cluster" lang="YBM Single" %}}

Examples requiring a single-node cluster can be run using the free [Sandbox](../yugabyte-cloud/cloud-basics/create-clusters/create-clusters-free/) cluster.

If you haven't already created your sandbox cluster, log in to YugabyteDB Managed, on the **Clusters** page click **Add Cluster**, and follow the instructions in the **Create Cluster** wizard.

  {{% /tab %}}

  {{% tab header="Multi-node cluster" lang="YBM Multi" %}}

Before you can create a multi-node cluster in YugabyteDB Managed, you need to [add your billing profile and payment method](../yugabyte-cloud/cloud-admin/cloud-billing-profile/), or you can [request a free trial](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431).

To create a single region three-node cluster, refer to [Create a single-region cluster](../yugabyte-cloud/cloud-basics/create-clusters/create-single-region/). Set **Fault tolerance** to **None** and **Nodes** to 3.

  {{% /tab %}}

{{< /tabpane >}}

Save your cluster credentials in a convenient location. You will use them to connect to your cluster.

**Connect to your clusters**

You can run Explore exercises in YugabyteDB Managed using the [Cloud Shell](../yugabyte-cloud/cloud-connect/connect-cloud-shell/):

1. In YugabyteDB Managed, on the **Clusters** page, select your cluster.
1. Click **Connect**.
1. Click **Launch Cloud Shell**.
1. Enter the user name from the cluster credentials you downloaded when you created the cluster.
1. Select the API to use (YSQL or YCQL) and click **Confirm**.
    The shell displays in a separate browser page. Cloud Shell can take up to 30 seconds to be ready.
1. Enter the password from the cluster credentials you downloaded when you created the cluster.

Note that if your Cloud Shell session is idle for more than 5 minutes, your browser may disconnect you. To resume, close the browser tab and connect again.

  </div>

  <div id="anywhere" class="tab-pane fade" role="tabpanel" aria-labelledby="anywhere-tab">

To run the examples, you need to create a single- or multi-node universe.

For instructions on creating a universe in YugabyteDB Anywhere, refer to [Create a multi-zone universe](../yugabyte-platform/create-deployments/create-universe-multi-zone/).

  </div>
</div>

## Set up YB Workload Simulator

YB Workload Simulator is a Java application that simulates workloads against YugabyteDB and provides live metrics of latency and throughput from the application's point of view. Some Explore topics use the application to demonstrate features of YugabyteDB.

The application uses the YugabyteDB JDBC [Smart Driver](../drivers-orms/smart-drivers/), which features universe- and topology-aware connection load balancing. The driver automatically balances application connections across the nodes in a universe, and rebalances connections when a node fails. For more information, see [YB Workload Simulator](https://github.com/YugabyteDB-Samples/yb-workload-simulator/).

### Download

YB Workload Simulator requires Java 11 or later installed on your computer. {{% jdk-setup %}}

Download the YB Workload Simulator JAR file using the following command:

```sh
wget https://github.com/YugabyteDB-Samples/yb-workload-simulator/releases/download/v0.0.4/yb-workload-sim-0.0.4.jar
```

## Use the application

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#localworkload" class="nav-link active" id="local-tab" data-toggle="tab"
      role="tab" aria-controls="local" aria-selected="true">
      <img src="/icons/database.svg" alt="Server Icon">
      Local
    </a>
  </li>
  <li >
    <a href="#cloudworkload" class="nav-link" id="cloud-tab" data-toggle="tab"
      role="tab" aria-controls="cloud" aria-selected="false">
      <img src="/icons/cloud.svg" alt="Cloud Icon">
      YugabyteDB Managed
    </a>
  </li>
  <li>
    <a href="#anywhereworkload" class="nav-link" id="anywhere-tab" data-toggle="tab"
      role="tab" aria-controls="anywhere" aria-selected="false">
      <img src="/icons/server.svg" alt="Server Icon">
      YugabyteDB Anywhere
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="cloudworkload" class="tab-pane fade" role="tabpanel" aria-labelledby="cloud-tab">

To connect the application to your cluster, ensure that you have downloaded the cluster SSL certificate and your computer is added to the IP allow list. Refer to [Before you begin](../develop/build-apps/cloud-add-ip/).

To start the application against a running YugabyteDB Managed cluster, use the following command:

```sh
java -Dnode=<host name> \
    -Ddbname=<dbname> \
    -Ddbuser=<dbuser> \
    -Ddbpassword=<dbpassword> \
    -Dssl=true \
    -Dsslmode=verify-full \
    -Dsslrootcert=<path-to-cluster-certificate> \
    -jar ./yb-workload-sim-0.0.4.jar
```

- `<host name>` - The host name of your YugabyteDB cluster. For YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
- `<dbname>` - The name of the database you are connecting to (the default is `yugabyte`).
- `<dbuser>` and `<dbpassword>` - The username and password for the YugabyteDB database. Use the credentials in the credentials file you downloaded when you created your cluster.
<!-- `<cloud.region.zone>` - The zones in your cluster, comma-separated, in the format `cloud.region.zone`, to be used as topology keys for [topology-aware load balancing](../drivers-orms/smart-drivers/#topology-aware-connection-load-balancing). Node details are displayed on the cluster **Nodes** tab. For example, to add topology keys for a multi-zone cluster in the AWS US East region, you would enter the following:

    ```sh
    -Dspring.datasource.hikari.data-source-properties.topologyKeys=aws.us-east-1.us-east-1a,aws.us-east-1.us-east-2a,aws.us-east-1.us-east-3a
    ```
-->
- `<path-to-cluster-certificate>` with the path to the [cluster certificate](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/) on your computer.

  </div>

  <div id="localworkload" class="tab-pane fade show active" role="tabpanel" aria-labelledby="local-tab">

To start the application against a running local universe, use the following command:

```sh
java -jar \
    -Dnode=127.0.0.1 \
    ./yb-workload-sim-0.0.4.jar
```

The `-Dnode` flag specifies the IP address of the node to which to connect.

The `-Dspring.datasource` flag enables [topology-aware load balancing](../drivers-orms/smart-drivers/#topology-aware-connection-load-balancing) for the application connections. If you created a universe using different zones, replace the zones with the corresponding zones in your universe, comma-separated, in the format `cloud.region.zone`.

  </div>

<div id="anywhereworkload" class="tab-pane fade" role="tabpanel" aria-labelledby="anywhere-tab">
<!--You start by moving the YB Workload Simulator JAR file from your local directory to the YugabyteDB Anywhere instance on AWS EC2, as follows:-->

<!--
```sh
scp -i <path_to_your_pem_file> yb-workload-sim-0.0.4.jar ec2-user@<YugabyteDB_Anywhere_instance_IP_address>:/tmp/
```
-->

<!-- For example:-->
<!--

```sh
scp -i Documents/Yugabyte/Security-Keys/AWS/AWS-east-1.pem yb-workload-sim-0.0.4.jar ec2-user@123.456.789.2XS:/tmp/
```
-->

To start the application against a running YugabyteDB Anywhere universe, use the following command from a local terminal:
<!-- You can launch the application from your YugabyteDB Anywhere instance by using the terminal, as follows:-->

<!--

  1. Navigate to your `tmp` directory and execute `mkdir logs` to create a log file in case there are any errors during the setup.
    2. Start the application against a running YugabyteDB Anywhere universe by executing the following commands in the terminal:

-->

```sh
java -Dnode=<node_ip> \
      -Ddbname=<dbname> \
      -Ddbuser=<dbuser> \
      -Ddbpassword=<dbpassword> \
      -Dspring.datasource.hikari.data-source-properties.topologyKeys=<aws.regions.zones> \
      -jar ./yb-workload-sim-0.0.4.jar
```

Replace the following:

- `<node_ip>` - The IP address of the node in your YugabyteDB Anywhere universe. You can find this information by navigating to **Universes > UniverseName > Nodes** in YugabyteDB Anywhere.

- `<dbname>` - The name of the database you are connecting to (the default is `yugabyte`).

- `<dbuser>` and `<dbpassword>` - The user name and password for the YugabyteDB database. <!-- - `<port>` - 5433. -->

- `<aws.regions.zones>` - The zones in your universe, comma-separated, in the format `cloud.region.zone`, to be used as topology keys for [topology-aware load balancing](../drivers-orms/smart-drivers/#topology-aware-connection-load-balancing). Node details are displayed in **Universes > UniverseName > Nodes**. For example, to add topology keys for a single-region multi-zone universe, you would enter the following:

    ```sh
    -Dspring.datasource.hikari.data-source-properties.topologyKeys=aws.us-east-1.us-east-1a,aws.us-east-1.us-east-1b,aws.us-east-1.us-east-1c
    ```

<!-- The preceding instructions are applicable to a YSQL workload.
To run a YCQL workload, add the following parameters before the `-jar ./yb-workload-sim-0.0.4.jar` command: -->

<!--
```sh
-Dworkload=genericCassandraWorkload \
-Dspring.data.cassandra.contact-points=<host_ip> \
-Dspring.data.cassandra.port=9042
-Dspring.data.cassandra.local-datacenter=<datacenter> [ex. us-east-2 ] \
-Dspring.data.cassandra.userid=cassandra \
-Dspring.data.cassandra.password=<cassandra_password> \
```
-->

<!-- Replace `<host_ip>`, `<datacenter>`, and `<cassandra_password>` with appropriate values.-->

<!--In the local environment, you would need to execute the following: -->

<!--
```sh
./mvnw spring-boot:run -Dspring-boot.run.profiles=dev
```
-->

</div>

To view the application UI, navigate to `http://<machine_ip_or_dns>:8080` (for example, `http://localhost:8080`).

### Start a read and write workload

You can start a workload that performs read and write operations across all the nodes of the universe as follows:

1. In the [application UI](http://localhost:8080), click the hamburger icon at the top of the page beside Active Workloads for Generic.
1. Select **Usable Operations**.
1. Under **Create Tables**, click **Run Create Tables Workload** to add tables to the database.
1. Under **Seed Data**, click **Run Seed Data Workload** to add data to the tables.
1. Under **Simulation**, select the **Include new Inserts** option, and click **Run Simulation Workload**.
1. Click **Close**.

The Latency and Throughput charts show the workload running on the universe.

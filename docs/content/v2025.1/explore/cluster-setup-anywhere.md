---
title: Set up clusters for running Explore examples
headerTitle: Run the examples
linkTitle: Run the examples
description: Set up clusters to run Explore examples.
headcontent: Set up YugabyteDB to run Explore examples
menu:
  v2025.1:
    identifier: cluster-setup-3-anywhere
    parent: explore
    weight: 5
type: docs
---

Use the following instructions to set up universes for running the examples in Explore.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../cluster-setup-local/" class="nav-link">
      <img src="/icons/database.svg" alt="Server Icon">
      Local
    </a>
  </li>
  <li >
    <a href="../cluster-setup-aeon/" class="nav-link">
      <img src="/icons/cloud.svg" alt="Cloud Icon">
      YugabyteDB Aeon
    </a>
  </li>
  <li>
    <a href="../cluster-setup-anywhere/" class="nav-link active">
      <img src="/icons/server.svg" alt="Server Icon">
      YugabyteDB Anywhere
    </a>
  </li>
</ul>


## Set up YugabyteDB universe

You can run examples using a universe, assuming you have [Installed](../../yugabyte-platform/install-yugabyte-platform/) YugabyteDB Anywhere and [configured](../../yugabyte-platform/configure-yugabyte-platform/) it to run in AWS.

To run the examples, you need to create a single- or multi-node universe.

For instructions on creating a universe in YugabyteDB Anywhere, refer to [Create a multi-zone universe](../../yugabyte-platform/create-deployments/create-universe-multi-zone/).

## Set up YB Workload Simulator

YB Workload Simulator is a Java application that simulates workloads against YugabyteDB and provides live metrics of latency and throughput from the application's point of view.

The application is used to demonstrate the following Explore topics:

- [Horizontal scalability](../linear-scalability/scaling-universe-yba/)
- [Resiliency](../fault-tolerance/macos-yba/)
- [Multi-region deployment](../multi-region-deployments/synchronous-replication-yba/)

The application uses the YugabyteDB JDBC [Smart Driver](/stable/develop/drivers-orms/smart-drivers/), which features universe- and topology-aware connection load balancing. The driver automatically balances application connections across the nodes in a universe, and re-balances connections when a node fails. For more information, see [YB Workload Simulator](https://github.com/YugabyteDB-Samples/yb-workload-simulator/).

### Download

YB Workload Simulator requires Java 11 or later installed on your computer. {{% jdk-setup %}}

Download the YB Workload Simulator JAR file using the following command:

```sh
wget https://github.com/YugabyteDB-Samples/yb-workload-simulator/releases/download/v0.0.8/yb-workload-sim-0.0.8.jar
```

### Use the application

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
      -Dspring.datasource.hikari.data-source-properties.topologyKeys=<cloud.region.zone> \
      -jar ./yb-workload-sim-0.0.8.jar
```

Replace the following:

- `<node_ip>` - The IP address of the node in your YugabyteDB Anywhere universe. You can find this information by navigating to **Universes > UniverseName > Nodes** in YugabyteDB Anywhere.

- `<dbname>` - The name of the database you are connecting to (the default is `yugabyte`).

- `<dbuser>` and `<dbpassword>` - The user name and password for the YugabyteDB database. <!-- - `<port>` - 5433. -->

- `<cloud.region.zone>` - The zones in your universe, comma-separated, in the format `cloud.region.zone`, to be used as topology keys for [topology-aware load balancing](/stable/develop/drivers-orms/smart-drivers/#topology-aware-load-balancing). Node details are displayed in **Universes > UniverseName > Nodes**. For example, to add topology keys for a single-region multi-zone universe, you would enter the following:

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

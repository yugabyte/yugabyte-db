---
title: Set up clusters for running Explore examples
headerTitle: Run the examples
linkTitle: Run the examples
description: Set up clusters to run Explore examples.
headcontent: Set up YugabyteDB to run Explore examples
menu:
  stable:
    identifier: cluster-setup-1-local
    parent: explore
    weight: 5
type: docs
---

{{< tip title="Docs MCP Server" >}}
Developing with YugabyteDB? Access the YugabyteDB Docs AI from your IDE or CLI. See [Docs MCP Server](../../reference/docs-mcp-server/).
{{< /tip >}}

Use the following instructions to set up universes for running the examples in Explore.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../cluster-setup-local/" class="nav-link active">
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
    <a href="../cluster-setup-anywhere/" class="nav-link">
      <img src="/icons/server.svg" alt="Server Icon">
      YugabyteDB Anywhere
    </a>
  </li>
</ul>

## Set up YugabyteDB universe

You can run examples using a universe set up on your local machine, assuming you have [Installed](/stable/quick-start/linux/) YugabyteDB.

Use the [yugabyted](../../reference/configuration/yugabyted/) utility to create and manage universes.

### Single-node universe

If a local universe is currently running, first [destroy it](../../reference/configuration/yugabyted/#destroy-a-local-universe).

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

For more information, refer to [Quick Start](/stable/quick-start/linux/#create-a-local-cluster).

### Multi-node universe

The following instructions show how to _simulate_ a multi-node universe on a single computer. To deploy an actual multi-zone universe using yugabyted, follow the instructions in [Create a multi-zone cluster](../../reference/configuration/yugabyted/#create-a-multi-zone-cluster).

{{<setup/local>}}

## Connect to universes

To run the examples in your universe, you use either the ysqlsh or ycqlsh CLI to interact with YugabyteDB via the YSQL or YCQL API.

You can start ysqlsh as follows:

```sh
./bin/ysqlsh
```

```output
ysqlsh (15.2-YB-{{<yb-version version="stable">}}-b0)
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

## Set up YB Workload Simulator

YB Workload Simulator is a Java application that simulates workloads against YugabyteDB and provides live metrics of latency and throughput from the application's point of view.

The application is used to demonstrate the following Explore topics:

- [Horizontal scalability](../linear-scalability/scaling-universe/)
- [Resiliency](../fault-tolerance/macos/)
- [Multi-region deployment](../multi-region-deployments/synchronous-replication-ysql/)

The application uses the YugabyteDB JDBC [Smart Driver](/stable/develop/drivers-orms/smart-drivers/), which features universe- and topology-aware connection load balancing. The driver automatically balances application connections across the nodes in a universe, and re-balances connections when a node fails. For more information, see [YB Workload Simulator](https://github.com/YugabyteDB-Samples/yb-workload-simulator/).

### Download

YB Workload Simulator requires Java 11 or later installed on your computer. {{% jdk-setup %}}

Download the YB Workload Simulator JAR file using the following command:

```sh
wget https://github.com/YugabyteDB-Samples/yb-workload-simulator/releases/download/v0.0.8/yb-workload-sim-0.0.8.jar
```

### Use the application

To start the application against a running local universe, use the following command:

```sh
java -jar \
    -Dnode=127.0.0.1 \
    ./yb-workload-sim-0.0.8.jar
```

The `-Dnode` flag specifies the IP address of the node to which to connect.

The `-Dspring.datasource` flag enables [topology-aware load balancing](/stable/develop/drivers-orms/smart-drivers/#topology-aware-load-balancing) for the application connections. If you created a universe using different zones, replace the zones with the corresponding zones in your universe, comma-separated, in the format `cloud.region.zone`.

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

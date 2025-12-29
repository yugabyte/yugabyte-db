---
title: Set up clusters for running Explore examples
headerTitle: Run the examples
linkTitle: Run the examples
description: Set up clusters to run Explore examples.
headcontent: Set up YugabyteDB to run Explore examples
menu:
  stable:
    identifier: cluster-setup-2-aeon
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
    <a href="../cluster-setup-aeon/" class="nav-link active">
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

## Set up YugabyteDB cluster

You can run examples using a cluster on YugabyteDB Aeon, assuming you have [Created an account](https://cloud.yugabyte.com/signup?utm_medium=direct&utm_source=docs&utm_campaign=Cloud_signup) in YugabyteDB Aeon.

### Sandbox cluster

Most examples in Explore can be run using the free [Sandbox](/stable/yugabyte-cloud/cloud-basics/create-clusters/create-clusters-free/) cluster.

If you haven't already created your sandbox cluster, sign in to YugabyteDB Aeon, on the **Clusters** page click **Add Cluster**, and follow the instructions in the **Create Cluster** wizard.

Save your cluster credentials in a convenient location. You will use them to connect to your cluster.

### Multi-node universe

Before you can create a multi-node cluster in YugabyteDB Aeon, you need to [add your billing profile and payment method](/stable/yugabyte-cloud/cloud-admin/cloud-billing-profile/), or you can [request a free trial](/stable/yugabyte-cloud/managed-freetrial/).

To create a single region three-node cluster, refer to [Create a single-region cluster](/stable/yugabyte-cloud/cloud-basics/create-clusters/create-single-region/). Set **Fault tolerance** to **None** and **Nodes** to 3.

Save your cluster credentials in a convenient location. You will use them to connect to your cluster.

## Connect to your clusters

You can run Explore exercises in YugabyteDB Aeon using the [Cloud Shell](/stable/yugabyte-cloud/cloud-connect/connect-cloud-shell/):

1. In YugabyteDB Aeon, on the **Clusters** page, select your cluster.
1. Click **Connect**.
1. Click **Launch Cloud Shell**.
1. Enter the user name from the cluster credentials you downloaded when you created the cluster.
1. Select the API to use (YSQL or YCQL) and click **Confirm**.
    The shell displays in a separate browser page. Cloud Shell can take up to 30 seconds to be ready.
1. Enter the password from the cluster credentials you downloaded when you created the cluster.

Note that if your Cloud Shell session is idle for more than 5 minutes, your browser may disconnect you. To resume, close the browser tab and connect again.

## Set up YB Workload Simulator

YB Workload Simulator is a Java application that simulates workloads against YugabyteDB and provides live metrics of latency and throughput from the application's point of view.

The application is used to demonstrate the following Explore topics:

- [Horizontal scalability](../linear-scalability/scaling-universe-cloud/)
- [Resiliency](../fault-tolerance/macos/) (this example is not available for Aeon)
- [Multi-region deployment](../multi-region-deployments/synchronous-replication-cloud/)

The application uses the YugabyteDB JDBC [Smart Driver](/stable/develop/drivers-orms/smart-drivers/), which features universe- and topology-aware connection load balancing. The driver automatically balances application connections across the nodes in a universe, and re-balances connections when a node fails. For more information, see [YB Workload Simulator](https://github.com/YugabyteDB-Samples/yb-workload-simulator/).

### Download

YB Workload Simulator requires Java 11 or later installed on your computer. {{% jdk-setup %}}

Download the YB Workload Simulator JAR file using the following command:

```sh
wget https://github.com/YugabyteDB-Samples/yb-workload-simulator/releases/download/v0.0.8/yb-workload-sim-0.0.8.jar
```

### Use the application

To connect the application to your cluster, ensure that you have downloaded the cluster SSL certificate and your computer is added to the IP allow list. Refer to [Before you begin](/stable/develop/tutorials/build-apps/cloud-add-ip/).

To start the application against a running YugabyteDB Aeon cluster, use the following command:

```sh
java -Dnode=<host name> \
    -Ddbname=<dbname> \
    -Ddbuser=<dbuser> \
    -Ddbpassword=<dbpassword> \
    -Dssl=true \
    -Dsslmode=verify-full \
    -Dsslrootcert=<path-to-cluster-certificate> \
    -jar ./yb-workload-sim-0.0.8.jar
```

- `<host name>` - The host name of your YugabyteDB cluster. For YugabyteDB Aeon, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
- `<dbname>` - The name of the database you are connecting to (the default is `yugabyte`).
- `<dbuser>` and `<dbpassword>` - The username and password for the YugabyteDB database. Use the credentials in the credentials file you downloaded when you created your cluster.
<!-- `<cloud.region.zone>` - The zones in your cluster, comma-separated, in the format `cloud.region.zone`, to be used as topology keys for [topology-aware load balancing](/stable/develop/drivers-orms/smart-drivers/#topology-aware-load-balancing). Node details are displayed on the cluster **Nodes** tab. For example, to add topology keys for a multi-zone cluster in the AWS US East region, you would enter the following:

    ```sh
    -Dspring.datasource.hikari.data-source-properties.topologyKeys=aws.us-east-1.us-east-1a,aws.us-east-1.us-east-2a,aws.us-east-1.us-east-3a
    ```
-->
- `<path-to-cluster-certificate>` with the path to the [cluster certificate](/stable/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/) on your computer.

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

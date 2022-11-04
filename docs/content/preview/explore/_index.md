---
title: Explore YugabyteDB
headerTitle: Explore YugabyteDB
linkTitle: Explore
headcontent: Learn about YugabyteDB features, with examples
description: Explore the features of YugabyteDB on macOS, Linux, Docker, and Kubernetes.
image: /images/section_icons/index/explore.png
aliases:
  - /preview/explore/cloud-native/
  - /preview/explore/transactional/
  - /preview/explore/high-performance/
  - /preview/explore/planet-scale/
  - /preview/explore/cloud-native/orchestration-readiness/
type: indexpage
showRightNav: true
---

The Explore section walks you through YugabyteDB's core features, with examples. Most examples demonstrating database features such as API compatibility can be run on a single-node cluster on your laptop or using the free Sandbox cluster in YugabyteDB Managed. More advanced scenarios use a multi-node deployment of YugabyteDB.

Explore assumes that you have either created an account in YugabyteDB Managed or installed YugabyteDB on your local computer.

| Section | Purpose | Setup |
| :--- | :--- | :--- |
| [SQL features](ysql-language-features/) | Learn about YugabyteDB's wire-compatibility with PostgreSQL, including data types, queries, expressions, operators and functions, and more. | Single-node cluster<br/>Local instance or YugabyteDB Managed |}
| [Going beyond SQL](ysql-language-features/going-beyond-sql/) | Learn about reducing read latency with follower reads and moving data closer to users with tablespaces. | Multi-node cluster<br/>Local instance |}
| [Fault tolerance](fault-tolerance/macos/) | Learn how YugabyteDB achieves high availability when a node goes down. | Multi-node cluster<br/>Local instance |
| [Horizontal scalability](linear-scalability/) | See how YugabyteDB handles loads while dynamically adding or removing nodes. | Multi-node cluster<br/>Local instance |
| [Transactions](transactions/) | Understand how distributed transactions and isolation levels work in YugabyteDB. | Single-node cluster<br/>Local instance or YugabyteDB Managed |
| [Indexes and constraints](indexes-constraints/) | Explore indexes in YugabyteDB, including primary and foreign keys, secondary, unique, partial, and expression indexes, and more. | Single-node cluster<br/>Local instance or YugabyteDB Managed |
| [JSON support](json-support/jsonb-ysql/) | YugabyteDB support for JSON is nearly identical to that in PostgreSQL. Learn about JSON-specific functions and operators available in YugabyteDB. | Single-node cluster<br/>Local instance or YugabyteDB Managed |
| [Multi-region deployments](multi-region-deployments/) | Learn about the different multi-region deployment topologies that can be deployed using YugabyteDB. | Multi-node cluster<br/>Local instance |
| [Query tuning](query-1-performance/) | Learn about the tools available for identifying and optimizing queries in YSQL. | Single-node cluster<br/>Local instance or YugabyteDB Managed |
| [Cluster management](cluster-management/) | Learn how to roll back database changes to a specific point in time using point in time recovery. | Single-node cluster<br/>Local instance |
| [Change data capture](change-data-capture/) | Learn about YugabyteDB support for streaming data to Kafka. | N/A |
| [Security](security/security/) | Learn how YugabyteDB secures your data, using authentication, authorization (RBAC), encryption, and more. | Single-node cluster<br/>Local instance or YugabyteDB Managed |
| [Observability](observability/) | Export metrics into Prometheus and create dashboards using Grafana. | Multi-node cluster<br/>Local instance |

## Set up your environment

To run the examples in explore, you'll need to create a single- or multi-node cluster.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="#cloud" class="nav-link active" id="cloud-tab" data-toggle="tab"
       role="tab" aria-controls="cloud" aria-selected="true">
      <i class="fas fa-cloud" aria-hidden="true"></i>
      Use a cloud cluster
    </a>
  </li>
  <li>
    <a href="#local" class="nav-link" id="local-tab" data-toggle="tab"
       role="tab" aria-controls="local" aria-selected="false">
      <i class="icon-shell" aria-hidden="true"></i>
      Use a local cluster
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="cloud" class="tab-pane fade show active" role="tabpanel" aria-labelledby="cloud-tab">

- Single node - Examples requiring a single-node cluster can be run using the free [Sandbox](../yugabyte-cloud/cloud-basics/create-clusters/create-clusters-free/) cluster.

  If you haven't already created your sandbox cluster, log in to YugabyteDB Managed, on the **Clusters** page click **Add Cluster**, and follow the instructions in the **Create Cluster** wizard.

  Save your cluster credentials in a convenient location, you will use them to connect to your cluster.

- Multi node - Before you can create a multi-node cluster in YugabyteDB Managed, you need to [add your billing profile and payment method](../yugabyte-cloud/cloud-admin/cloud-billing-profile/), or you can [request a free trial](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431).

  To create a single region multi-node cluster, refer to [Create a single-region cluster](../yugabyte-cloud/cloud-basics/create-clusters/create-single-region/).

**Connect to your clusters**

You can run Explore exercises in YugabyteDB Managed using the Cloud Shell:

1. In YugabyteDB Managed, on the **Clusters** page, select your cluster.
1. Click **Connect**.
1. Click **Launch Cloud Shell**.
1. Enter the user name from the cluster credentials you downloaded when you created the cluster.
1. Select the API to use (YSQL or YCQL) and click **Confirm**.
  The shell displays in a separate browser page. Cloud Shell can take up to 30 seconds to be ready.
1. Enter the password from the cluster credentials you downloaded when you created the cluster.

Note that if your Cloud Shell session is idle for more than 5 minutes, your browser may disconnect you. To resume, close the browser tab and connect again.

  </div>
  <div id="local" class="tab-pane fade" role="tabpanel" aria-labelledby="local-tab">

For testing and learning YugabyteDB on your computer, use the [yugabyted](../reference/configuration/yugabyted/) cluster management utility.

{{< tabpane text=true >}}

  {{% tab header="Single-node cluster" lang="Single-node cluster" %}}

You can create a single-node local cluster with a replication factor (RF) of 1 by running the following command:

```sh
./bin/yugabyted start
```

Or, if you are running macOS Monterey:

```sh
./bin/yugabyted start --master_webserver_port=9999
```

For more information, refer to [Quick Start](../quick-start/linux/#create-a-local-cluster).

  {{% /tab %}}

  {{% tab header="Multi-node cluster" lang="Multi-node cluster" %}}

If a single-node cluster is currently running, first destroy the running cluster as follows:

```sh
./bin/yugabyted destroy
```

Start a local three-node cluster with a replication factor of `3`by first creating a single node cluster as follows:

```sh
./bin/yugabyted start \
                --listen=127.0.0.1 \
                --base_dir=/tmp/ybd1
```

Next, join two more nodes with the previous node. By default, [yugabyted](../reference/configuration/yugabyted/) creates a cluster with a replication factor of `3` on starting a 3 node cluster.

```sh
./bin/yugabyted start \
                --listen=127.0.0.2 \
                --base_dir=/tmp/ybd2 \
                --join=127.0.0.1
```

```sh
./bin/yugabyted start \
                --listen=127.0.0.3 \
                --base_dir=/tmp/ybd3 \
                --join=127.0.0.1
```

  {{% /tab %}}

{{< /tabpane >}}

**Connect to clusters**

To run the examples in your cluster, you use either the ysqlsh or ycqlsh CLI to interact with YugabyteDB using the YSQL or YCQL API.

To start ysqlsh:

```sh
$ ./bin/ysqlsh
```

```output
ysqlsh (11.2-YB-2.0.0.0-b0)
Type "help" for help.

yugabyte=#
```

To start ycqlsh:

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
</div>

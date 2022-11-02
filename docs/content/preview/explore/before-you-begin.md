---
title: Before you begin
headerTitle: Before you begin
description: Obtain your cluster certificate and add your computer to the IP allow list.
headcontent: Get ready to explore YugabyteDB Managed
menu:
  preview:
    parent: explore
    name: Before you begin
    identifier: before-you-begin
    weight: 20
type: docs
---

The Explore section walks you through YugabyteDB's core features, with examples. Most examples demonstrating database features such as API compatibility can be run on a single-node cluster on your laptop or in YugabyteDB Managed. Examples requiring multiple nodes use more complex multi-node deployments that can also be simulated on your laptop.

Before you start to explore, you may want to familiarize yourself with YugabyteDB by running the [Quick Start](../../quick-start/).

Explore assumes that you have either created an account in YugabyteDB Managed or installed YugabyteDB on your local computer.

Use the following instructions to create clusters so that you can try out the examples yourself.

## YugabyteDB Managed

Signing up for YugabyteDB Managed is free, and you can create a free single-node Sandbox cluster without using a credit card:

- [Sign up](https://cloud.yugabyte.com/signup?utm_medium=direct&utm_source=docs&utm_campaign=YBM_signup).
- [Log in](https://cloud.yugabyte.com/login).

The first time you log in, YugabyteDB Managed provides a welcome experience with a 15 minute guided tutorial. Complete the steps in the Get Started tutorial to do the following:

- Create a Sandbox (single node) cluster
- Connect to the database
- Load sample data and run queries
- Explore a sample application

For more information, refer to the [Quick Start](../../yugabyte-cloud/cloud-quickstart/).

### Multi-node cluster

Before you can create a multi-node cluster in YugabyteDB Managed, you need to [add your billing profile and payment method](../../yugabyte-cloud/cloud-admin/cloud-billing-profile/), or you can [request a free trial](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431).

To create a single region multi-node cluster, refer to [Create a single-region cluster](../../yugabyte-cloud/cloud-basics/create-clusters/create-single-region/).

### Connect to your cluster

You can run Explore exercises in YugabyteDB Managed using the Cloud Shell:

1. In YugabyteDB Managed, on the **Clusters** page, select your cluster.
1. Click **Connect**.
1. Click **Launch Cloud Shell**.
1. Enter the user name from the cluster credentials you downloaded when you created the cluster.
1. Select the API to use (YSQL or YCQL) and click **Confirm**.
  The shell displays in a separate browser page. Cloud Shell can take up to 30 seconds to be ready.
1. Enter the password from the cluster credentials you downloaded when you created the cluster.

Note that if your Cloud Shell session is idle for more than 5 minutes, your browser may disconnect you. To resume, close the browser tab and connect again.

For more information, refer to [Connect to clusters in YugabyteDB Managed](../../yugabyte-cloud/cloud-connect/).

## Local install

Download and install YugabyteDB as follows:

1. Download the YugabyteDB `tar.gz` file by executing the following command:

    ```sh
    curl -O https://downloads.yugabyte.com/releases/{{< yb-version version="preview">}}/yugabyte-{{< yb-version version="preview" format="build">}}-darwin-x86_64.tar.gz
    ```

1. Extract the package and then change directories to the YugabyteDB home, as follows:

    ```sh
    tar xvfz yugabyte-{{< yb-version version="preview" format="build">}}-darwin-x86_64.tar.gz && cd yugabyte-{{< yb-version version="preview">}}/
    ```

1. If you are running Linux, run the following script:

    ```sh
    ./bin/post_install.sh
    ```

For more information, refer to [Quick Start](../../quick-start/linux/#install-yugabytedb).

### Create a single-node cluster

For testing and learning YugabyteDB on your computer, use the [yugabyted](../../reference/configuration/yugabyted/) cluster management utility.

You can create a single-node local cluster with a replication factor (RF) of 1 by running the following command:

```sh
./bin/yugabyted start
```

Or, if you are running macOS Monterey:

```sh
./bin/yugabyted start --master_webserver_port=9999
```

For more information, refer to [Quick Start](../../quick-start/linux/#create-a-local-cluster).

### Create a multi-node cluster

Start a local three-node cluster with a replication factor of `3`by first creating a single node cluster as follows:

```sh
./bin/yugabyted start \
                --listen=127.0.0.1 \
                --base_dir=/tmp/ybd1
```

Next, join two more nodes with the previous node. By default, [yugabyted](../../reference/configuration/yugabyted/) creates a cluster with a replication factor of `3` on starting a 3 node cluster.

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

### Destroy a cluster

If you need to create a different type of cluster and a cluster is already running, you can destroy the running cluster as follows:

```sh
./bin/yugabyted destroy
```

### Connect to your cluster


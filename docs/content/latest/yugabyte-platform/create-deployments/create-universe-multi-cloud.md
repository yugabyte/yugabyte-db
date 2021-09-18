---
title: Create a multi-cloud universe using Yugabyte Platform
headerTitle: Create a multi-cloud universe
linkTitle: Multi-cloud universe
description: Use Yugabyte Platform to create a YugabyteDB universe that spans multiple cloud providers.
menu:
  latest:
    identifier: create-multi-cloud-universe
    parent: create-deployments
    weight: 35
isTocNested: true
showAsideToc: true
---

This page describes how to create a YugabyteDB universe spanning multiple geographic regions and cloud providers. In this example, you'll deploy a single Yugabyte universe across AWS (US-West-2), Google Cloud (Central-1), and Microsoft Azure (US-East1). 

To do this, you'll need to:

* [Check the prerequisites](#prerequisites)
* [Set up node instance VMs](#set-up-instance-vms) in each cloud (AWS, GCP, and Azure)
* [Set up VPC peering](#set-up-vpc-peering) through a VPN tunnel across these 3 clouds
* [Deploy Yugabyte Platform](#deploy-yugabyte-platform) on one of the nodes
* [Deploy a Yugabyte universe](#deploy-a-universe) on your multi-cloud topology
* [Run a global application](#run-a-global-application)

## Prerequisites

* Ability to create VPC, subnets and provision VMs in each of the clouds of choice
* Root access on the instances to be able to create the Yugabyte user
* The instances should have access to the internet to download software (not required, but helpful)

## Set up instance VMs

When you create a universe, you'll need to import nodes that can be managed by Yugabyte Platform. To set up your nodes, follow the instructions on the [Prepare nodes (on-prem)](/latest/yugabyte-platform/install-yugabyte-platform/prepare-on-prem-nodes/) page.

Notes on node instances:

* Your nodes across different cloud providers should be of similar configuration &mdash; vCPUs, DRAM, storage, and networking.
* For more information on ports used by YugabyteDB, refer to [Default ports](../../../reference/configuration/default-ports).
* Ensure that your YugabyteDB nodes conform to the requirements outlined in the [deployment checklist](/latest/deploy/checklist/). This checklist also gives an idea of [recommended instance types across public clouds](/latest/deploy/checklist/#running-on-public-clouds).

## Set up VPC peering

Next, set up multi-cloud VPC peering through a VPN tunnel.

Yugabyte is a distributed SQL database, and requires TCP/IP communication across nodes and requires a particular [set of firewall ports](latest/yugabyte-platform/install-yugabyte-platform/prepare-on-prem-nodes/#ports) to be opened for cluster operations, which you set up in the previous section.

You should use non-overlapping CIDR blocks for each subnet across different clouds.

All public cloud providers enable VPN tunneling across VPCs and their subnet to enable network discovery. As an example, refer to [this tutorial](https://medium.com/google-cloud/vpn-between-two-clouds-e2e3578be773) on connecting an AWS VPC to Google’s Cloud Platform over a VPN.

## Install Yugabyte Platform

Follow these steps on the [Install Yugabyte Platform](/latest/yugabyte-platform/install-yugabyte-platform/) page to deploy Yugabyte Platform on a new VM on one of your cloud providers. You'll use this node to manage your YugabyteDB universe.

## Configure the on-premises cloud provider

This section outlines now to configure the on-premises cloud provider for YugabyteDB using the Yugabyte Platform console. If no cloud providers are configured, the main Dashboard page highlights that you need to configure at least one cloud provider. Refer to [Configure the on-premises cloud provider](/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/on-premises/) for more information.

To configure the on-premises cloud provider, do the following:

1. asdfasdf.

    ![caption](/images/ee/multi-cloud-create-universe.png)

1. asdfasdf.

    ![caption](/images/ee/multi-cloud-create-universe.png)

1. asdfasdf.

    ![caption](/images/ee/multi-cloud-create-universe.png)

1. asdfasdf.

    ![caption](/images/ee/multi-cloud-create-universe.png)

1. asdfasdf.

    ![caption](/images/ee/multi-cloud-create-universe.png)

### Define instance types

On the Instances tab...

For each provider, define an instance type...

### Define regions

On the On-Premises Datacenter tab, click Regions and Zones...

### Add instances

Navigate to Configs > On-Premises Datacenter, and click Manage Instances...

## Deploy a universe

If no universes have been created yet, the Yugabyte Platform dashboard looks similar to the following:

![Dashboard with no universes](/images/ee/no-univ-dashboard.png)

To create a multi-region universe, do the following:

1. Click Create Universe.

1. Enter the universe name: `multi-cloud-demo-6node`

1. Enter the set of regions: `us-aws-west-2`, `us-azu-east-1`, `us-centra1-b`

1. Change instance type to `8core`.

1. Add the following flags for Master and T-Server:

    * `leader_failure_max_missed_heartbeat_periods=10`
        \
        Because the data is globally replicated, RPC latencies are higher. This flag increases the failure-detection interval to compensate.
        \
        And because deployments on public clouds require security:
    
    * `use_cassandra_authentication=true`
    * `ysql_enable_auth=true`

1. Click Create at the bottom right.

At this point, Yugabyte Platform begins to provision your new universe across multiple cloud providers.

## Run a global application

In this section, we are going to connect to each node and perform the following:

* Run the `CassandraKeyValue` workload
* Write data with global consistency (higher latencies because we chose nodes in far away regions)
* Read data from the local data center (low latency, timeline-consistent reads)

Browse to the **Nodes** tab to find the nodes and click **Connect**. This should bring up a dialog showing how to connect to the nodes.

![Multi-region universe nodes](/images/ee/multi-region-universe-nodes-connect.png)

### Connect to the nodes

Create three Bash terminals and connect to each of the nodes by running the commands shown in the popup above. We are going to start a workload from each of the nodes.

On each of the terminals, do the following:

1. Install Java.

    ```sh
    $ sudo yum install java-1.8.0-openjdk.x86_64 -y
    ```

1. Switch to the `yugabyte` user.

    ```sh
    $ sudo su - yugabyte
    ```

1. Export the `YCQL_ENDPOINTS` environment variable.

    \
    Browse to the **Universe Overview** tab in Yugabyte Platform console and click **YCQL Endpoints**. A new tab opens displaying a list of IP addresses.

    \
    Export this into a shell variable on the database node `yb-dev-helloworld1-n1` you connected to. Remember to replace the IP addresses below with those shown in the Yugabyte Platform console.

    ```sh
    $ export YCQL_ENDPOINTS="10.138.0.3:9042,10.138.0.4:9042,10.138.0.5:9042"
    ```

### Run the workload

Run the following command on each of the nodes. Remember to substitute `<REGION>` with the region code for each node.

```sh
$ java -jar /home/yugabyte/tserver/java/yb-sample-apps.jar \
            --workload CassandraKeyValue \
            --nodes $YCQL_ENDPOINTS \
            --num_threads_write 1 \
            --num_threads_read 32 \
            --num_unique_keys 10000000 \
            --local_reads \
            --with_local_dc <REGION>
```

You can find the region codes for each of the nodes by browsing to the **Nodes** tab for this universe in the Yugabyte Platform console. A screenshot is shown below. In this example, the value for `<REGION>` is:

* `us-east4` for node `yb-dev-helloworld2-n1`
* `asia-northeast1` for node `yb-dev-helloworld2-n2`
* `us-west1` for node `yb-dev-helloworld2-n3`

![Region Codes For Universe Nodes](/images/ee/multi-region-universe-node-regions.png)

### Check the performance characteristics of the app

Recall that we expect the app to have the following characteristics based on its deployment configuration:

* Global consistency on writes, which would cause higher latencies in order to replicate data across multiple geographic regions.
* Low latency reads from the nearest data center, which offers timeline consistency (similar to async replication).

You can verify this by browsing to the **Metrics** tab of the universe in the Yugabyte Platform console to see the overall performance of the app. It should look similar to the following screenshot.

![YCQL Load Metrics](/images/ee/multi-region-read-write-metrics.png)

Note the following:

* Write latency is higher because it has to replicate data to a quorum of nodes across multiple regions and providers.
* Read latency is low across all nodes.

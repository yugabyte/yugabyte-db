---
title: Create a multi-cloud universe using Yugabyte Platform
headerTitle: Create a multi-cloud universe
linkTitle: Multi-cloud universe
description: Use Yugabyte Platform to create a YugabyteDB universe that spans multiple cloud providers.
menu:
  stable:
    identifier: create-multi-cloud-universe
    parent: create-deployments
    weight: 35
isTocNested: true
showAsideToc: true
---

This page describes how to create a YugabyteDB universe spanning multiple geographic regions and cloud providers. In this example, you'll deploy a single Yugabyte universe across AWS (US-West-2), Google Cloud (Central-1), and Microsoft Azure (US-East1). 

The universe topology will be as follows:

![Multi-cloud universe topology](/images/ee/multi-cloud-topology.png)

To do this, you'll need to:

* [Check the prerequisites](#prerequisites)
* [Set up node instance VMs](#set-up-instance-vms) in each cloud (AWS, GCP, and Azure)
* [Set up VPC peering](#set-up-vpc-peering) through a VPN tunnel across these 3 clouds
* [Install Yugabyte Platform](#install-yugabyte-platform) on one of the nodes
* [Deploy a Yugabyte universe](#deploy-a-universe) on your multi-cloud topology
* [Run the TPC-C benchmark](#run-the-tpc-c-benchmark)

## Prerequisites

* Ability to create VPC, subnets and provision VMs in each of the clouds of choice
* Root access on the instances to be able to create the Yugabyte user
* The instances should have access to the internet to download software (not required, but helpful)

## Set up instance VMs

When you create a universe, you'll need to import nodes that can be managed by Yugabyte Platform. To set up your nodes, follow the instructions on the [Prepare nodes (on-prem)](../../install-yugabyte-platform/prepare-on-prem-nodes/) page.

Notes on node instances:

* Your nodes across different cloud providers should be of similar configuration &mdash; vCPUs, DRAM, storage, and networking.
* For more information on ports used by YugabyteDB, refer to [Default ports](../../../reference/configuration/default-ports).
* Ensure that your YugabyteDB nodes conform to the requirements outlined in the [deployment checklist](../../../deploy/checklist/). This checklist also gives an idea of [recommended instance types across public clouds](../../../deploy/checklist/#running-on-public-clouds).

## Set up VPC peering

Next, set up multi-cloud VPC peering through a VPN tunnel.

Yugabyte is a distributed SQL database, and requires TCP/IP communication across nodes and requires a particular [set of firewall ports](../../install-yugabyte-platform/prepare-on-prem-nodes/#ports) to be opened for cluster operations, which you set up in the previous section.

You should use non-overlapping CIDR blocks for each subnet across different clouds.

All public cloud providers enable VPN tunneling across VPCs and their subnet to enable network discovery. As an example, refer to [this tutorial](https://medium.com/google-cloud/vpn-between-two-clouds-e2e3578be773) on connecting an AWS VPC to Google’s Cloud Platform over a VPN.

## Install Yugabyte Platform

Follow these steps on the [Install Yugabyte Platform](../../install-yugabyte-platform/) page to deploy Yugabyte Platform on a new VM on one of your cloud providers. You'll use this node to manage your YugabyteDB universe.

## Configure the on-premises cloud provider

This section outlines now to configure the on-premises cloud provider for YugabyteDB using the Yugabyte Platform console. If no cloud providers are configured, the main Dashboard page highlights that you need to configure at least one cloud provider. Refer to [Configure the on-premises cloud provider](../../configure-yugabyte-platform/set-up-cloud-provider/on-premises/) for more information.

Follow the instructions in the next sub-sections to configure your cloud provider, instance types, and regions, and to provision the instances you'll use in your universe.

### Set up the cloud provider

On the Provider Info tab, configure the cloud provider as follows: 

* **Provider Name** is `multi-cloud-demo`.
* **SSH User** is the user which will run Yugabyte on the node (yugabyte in this case).
* **SSH Port** should remain the default of `22` unless your servers have a different SSH port.
* **Manually Provision Nodes** should be set to off so that Platform will install the software on these nodes.
* **SSH Key** is the contents of the private key file to be used for authentication.
  \
  Note that Paramiko is used for SSH validation, which typically doesn't accept keys generated with OpenSSL. If you generate your keys with OpenSSL, use a format similar to:

    ```sh
    ssh-keygen -m PEM -t rsa -b 2048 -f test_id_rsa
    ```

* Air Gap install should only be on if your nodes don't have internet connectivity.

![caption](/images/ee/multi-cloud-provider-info.png)

### Define an instance type

On the Instance Types tab, enter a machine description which matches the nodes you will be using. The machine type can be any logical name, given the machine types will be different between all 3 regions. In this example, use `8core`.

![Multi-cloud instance description](/images/ee/multi-cloud-instances.png)

### Define regions

On the Regions and Zones tab, define your regions.

It can be tricky to identify which nodes are in which clouds, so you should use descriptive names.

![Multi-cloud regions](/images/ee/multi-cloud-regions.png)

### Save the provider

Click Finish to create your cloud provider. Once fully configured, the provider should look similar to the following:

![Multi-cloud provider map view](/images/ee/multi-cloud-provider-map.png)

### Provision instances

Once you've defined your cloud provider configuration, click Manage Instances to provision as many nodes as your application requires. Follow the instructions in Step 2 of the [Configure the on-premises cloud provider](../../configure-yugabyte-platform/set-up-cloud-provider/on-premises/#step-2-provision-the-yugabytedb-nodes) page.

The provider's instance list should be similar to this:

![Multi-cloud instance list](/images/ee/multi-cloud-provider-instance-list.png)

## Deploy a universe

To create a multi-region universe, do the following:

1. On the Dashboard or Universes page, click Create Universe.

    ![New universe details](/images/ee/multi-cloud-create-universe.png)

1. Enter the universe name: `multi-cloud-demo-6node`

1. Enter the set of regions: `us-aws-west-2`, `us-azu-east-1`, `us-centra1-b`

1. Set instance type to `8core`.

1. Add the following flags for Master and T-Server:

    * `leader_failure_max_missed_heartbeat_periods=10`
    \
    Because the data is globally replicated, RPC latencies are higher. This flag increases the failure-detection interval to compensate.
    \
    And because deployments on public clouds require security:
    
    * `use_cassandra_authentication=true`
    * `ysql_enable_auth=true`

1. Click Create at the bottom right.

At this point, Yugabyte Platform begins to provision your new universe across multiple cloud providers. When the universe is provisioned, it appears on the Dashboard and Universes pages. Click the universe name to open its overview page.

![Universe overview page](/images/ee/multi-cloud-universe-overview.png)

The universe's nodes list will be similar to the following:

![Universe overview](/images/ee/multi-cloud-universe-nodes.png)

## Run the TPC-C benchmark

To run the TPC-C benchmark on your universe, you can use commands similar to the following (with your own IP addresses):

```sh
./tpccbenchmark -c config/workload_all.xml \
    --create=true \
    --nodes=10.9.4.142,10.14.16.8,10.9.13.138,10.14.16.9,10.152.0.14,10.152.0.32 \
    --warehouses 50 \
    --loaderthreads 10
./tpccbenchmark -c config/workload_all.xml \
    --load=true \
    --nodes=10.9.4.142,10.14.16.8,10.9.13.138,10.14.16.9,10.152.0.14,10.152.0.32 \
    --warehouses 50 \
    --loaderthreads 10
./tpccbenchmark -c config/workload_all.xml \
    --load=true \
    --nodes=10.9.4.142,10.14.16.8,10.9.13.138,10.14.16.9,10.152.0.14,10.152.0.32 \
    --warehouses 50 
```

Refer to [Running TPC-C on Yugabyte](../../../benchmark/tpcc-ysql/) for more details.

---
title: Create a multi-cloud universe
headerTitle: Create a multi-cloud universe
linkTitle: Multi-cloud universe
description: Use YugabyteDB Anywhere to create a YugabyteDB universe that spans multiple cloud providers.
menu:
  v2.16_yugabyte-platform:
    identifier: create-multi-cloud-universe
    parent: create-deployments
    weight: 35
type: docs
---

You can use YugabyteDB Anywhere to create a YugabyteDB universe spanning multiple geographic regions and cloud providers. For example, you can deploy a single universe across AWS (US-West-2), Google Cloud Provider (Central-1), and Microsoft Azure (US-East1).

The following depicts the universe topology:

![Multi-cloud universe topology](/images/ee/multi-cloud-topology.png)

To create a multi-cloud universe, you would need to do the following:

* [Check prerequisites](#prerequisites)
* [Set up node instance virtual machines](#set-up-instance-vms) in each cloud (AWS, GCP, and Azure)
* [Set up VPC peering](#set-up-vpc-peering) through a VPN tunnel across these 3 clouds
* [Install YugabyteDB Anywhere](#install-yugabytedb-anywhere) on one of the nodes
* [Create a universe](#create-a-universe) on your multi-cloud topology
* [Run the TPC-C benchmark](#run-the-tpc-c-benchmark)

## Prerequisites

The following requirements must be met:

* You must be able to create a Virtual Private Cloud (VPC), subnets, and provision virtual machines (VMs) in every cloud provider that you want to use.
* You need root access on the instances to be able to create the Yugabyte user.
* The instances should have access to the Internet to download software (recommended).

## Set up instance VMs

When you create a universe, you need to import nodes that can be managed by YugabyteDB Anywhere. To set up your nodes, follow instructions in [Prepare nodes (on-premises)](../../install-yugabyte-platform/prepare-on-prem-nodes/).

Note the following:

* Your nodes across different cloud providers should be of similar configuration: vCPUs, DRAM, storage, and networking.
* For more information on ports used by YugabyteDB, refer to [Default ports](../../../reference/configuration/default-ports/).
* Ensure that your YugabyteDB nodes conform to the requirements outlined in the [deployment checklist](../../../deploy/checklist/), which gives an idea of [recommended instance types across public clouds](../../../deploy/checklist/#running-on-public-clouds).

## Set up VPC peering

You need to set up multi-cloud VPC peering through a VPN tunnel.

YugabyteDB is a distributed SQL database and requires TCP/IP communication across nodes. It also requires a particular [set of firewall ports](../../install-yugabyte-platform/prepare-on-prem-nodes/#ports) to be opened for cluster operations, which you set up in the previous section.

You should use non-overlapping Classless Inter-Domain Routing (CIDR) blocks for each subnet across different clouds.

All public cloud providers enable VPN tunneling across VPCs and their subnet to enable network discovery. As an example, see [VPN between two clouds](https://medium.com/google-cloud/vpn-between-two-clouds-e2e3578be773).

## Install YugabyteDB Anywhere

Follow steps provided in [Install YugabyteDB Anywhere](../../install-yugabyte-platform/) to deploy YugabyteDB Anywhere on a new VM on one of your cloud providers. You will be able to use this node to manage your YugabyteDB universe.

## Configure the on-premises cloud provider

You can configure the on-premises cloud provider for YugabyteDB using YugabyteDB Anywhere. If no cloud providers are configured, the main **Dashboard** page highlights that you need to configure at least one cloud provider. Refer to [Configure the on-premises cloud provider](../../configure-yugabyte-platform/set-up-cloud-provider/on-premises/) for more information.

### Set up the cloud provider

You need to navigate to **Configs > On-Premises Datacenters**, click **Add Configuration**, and then select  **Provider Info**. Enter sample values into the following fields:

* **Provider Name** is `multi-cloud-demo`.
* **SSH User** is the user which will run Yugabyte on the node (yugabyte in this case).
* **SSH Port** should remain the default of `22` unless your servers have a different SSH port.
* **Manually Provision Nodes** should be disabled so that YugabyteDB Anywhere installs the software on these nodes.
* **SSH Key** is the contents of the private key file to be used for authentication.
  \
  Note that Paramiko is used for SSH validation, which typically does not accept keys generated with OpenSSL. If you generate your keys with OpenSSL, use a format similar to the following:

    ```sh
  ssh-keygen -m PEM -t rsa -b 2048 -f test_id_rsa
    ```

* **Air Gap Install** should only be enabled if your nodes do not have the Internet connectivity.<br>

  ![caption](/images/ee/multi-cloud-provider-info.png)

### Define an instance type

Select **Instance Types** and enter a machine description which matches the nodes you will be using. The machine type can be any logical name, given the machine types will be different between all three regions. The following example uses `8core`:

![Multi-cloud instance description](/images/ee/multi-cloud-instances.png)

### Define regions

Select **Regions and Zones** and define your regions, using descriptive names, as per the following illustration:

![Multi-cloud regions](/images/ee/multi-cloud-regions.png)

Click **Finish** to create your cloud provider. Once fully configured, the provider should look similar to the following:

![Multi-cloud provider map view](/images/ee/multi-cloud-provider-map.png)

### Provision instances

Once you have defined your cloud provider configuration, click **Manage Instances** to provision as many nodes as your application requires. Follow the instructions provided in [Configure the on-premises cloud provider](../../configure-yugabyte-platform/set-up-cloud-provider/on-premises/#add-yugabytedb-nodes).

The provider's instance list should be similar to the following:

![Multi-cloud instance list](/images/ee/multi-cloud-provider-instance-list.png)

## Create a universe

You can create a multi-region universe as follows:

1. Navigate to **Dashboard** or **Universes** and click **Create Universe**.

1. Complete the **Primary Cluster** fields, as shown in the following illustration:<br>

    ![New universe details](/images/ee/multi-cloud-create-universe.png)

1. Enter the universe name: `multi-cloud-demo-6node`

1. Enter the set of regions: `us-aws-west-2`, `us-azu-east-1`, `us-centra1-b`

1. Set the instance type to `8core`.

1. Add the following flags for Master and T-Server:

    * `leader_failure_max_missed_heartbeat_periods=10` - Since the data is globally replicated, RPC latencies are higher. This flag increases the failure-detection interval to compensate.

    * `use_cassandra_authentication=true` - Deployments on public clouds require security.
    * `ysql_enable_auth=true` - Deployments on public clouds require security.

1. Click **Create**.

At this point, YugabyteDB Anywhere begins to provision your new universe across multiple cloud providers. When the universe is provisioned, it appears on the **Dashboard** and **Universes**. You can click the universe name to open its **Overview**.

![Universe overview page](/images/ee/multi-cloud-universe-overview.png)

The new universe's nodes list will be similar to the following:

![Universe overview](/images/ee/multi-cloud-universe-nodes.png)

## Run the TPC-C benchmark

To run the TPC-C benchmark on your universe, use commands similar to the following (with your own IP addresses):

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

Refer to [Running TPC-C on Yugabyte](../../../benchmark/tpcc-ysql/) for details.

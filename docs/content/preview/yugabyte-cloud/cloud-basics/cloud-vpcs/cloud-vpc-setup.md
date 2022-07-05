<!---
title: Set up a VPC network
headerTitle:
linkTitle: Set up a VPC network
description: How to set up and VPC network in YugabyteDB Managed.
menu:
  preview_yugabyte-cloud:
    identifier: cloud-vpc-setup
    parent: cloud-vpcs
    weight: 20
--->

## Before you begin

Before setting up the VPC network, you'll need the following:

- The CIDR block you want to use for your VPC.

  - Refer to [Setting the CIDR and sizing your VPC](../cloud-vpc-intro/#setting-the-cidr-and-sizing-your-vpc).

- The details of the application VPC you want to peer with.

  - AWS - the AWS account ID, and the VPC ID, region, and CIDR block. To obtain these details, navigate to your AWS [Your VPCs](https://console.aws.amazon.com/vpc/home?#vpcs) page for the region where the VPC is located.

  - GCP - the project ID and the network name, and CIDR block. To obtain these details, navigate to your GCP [VPC networks](https://console.cloud.google.com/networking/networks) page.

## Tasks

To create a VPC network, you need to complete the following tasks. With the exception of 3, these tasks are performed in YugabyteDB Managed.

### 1. Create a VPC

The first step is to [create the VPC](../cloud-add-vpc/#create-a-vpc) where you will deploy your YugabyteDB Managed cluster.

The VPC reserves a range of IP addresses for the network. The range can't overlap with the range used by any application VPC you want to peer.

VPCs are configured on the [VPCs](../cloud-add-vpc/) page of the **VPC Network** tab on the **Network Access** page.

The status of the VPC is _Active_ when done.

After the VPC is created, you can [deploy a cluster in the VPC](../cloud-add-vpc/#deploy-a-cluster-in-a-vpc); you don't need to wait until the VPC is peered.

### 2. Create a peering connection

Next, [create a peering connection](../cloud-add-peering/) between your VPC and the application VPC on the cloud provider network.

Peering connections are configured on the **Peering Connections** page of the **VPC Network** tab on the **Network Access** page.

The status of the peering connection is _Pending_ when done; to make the connection active, you must configure your cloud provider.

### 3. Configure the cloud provider

After the VPC and peering connection are created in YugabyteDB Managed, [configure your cloud provider](../cloud-configure-provider) to confirm the connection:

- In AWS, accept the peering request.
- In GCP, create a peering connection.

The status of the peering connection changes to _Active_ once communication is established.

### 4. Add the application VPC to the IP allow list

To communicate with a cluster, networks must be added to the cluster IP allow list. This includes peered application VPCs.

After the VPC and the peering connection are active, add at least one of the CIDR blocks associated with the peered application VPC to the [IP allow list](../../../cloud-secure-clusters/add-connections/) for your cluster.

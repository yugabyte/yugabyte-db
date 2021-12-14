---
title: Set up a VPC network
headerTitle: 
linkTitle: Set up a VPC network
description: How to set up and VPC network in Yugabyte Cloud.
menu:
  latest:
    identifier: cloud-vpc-setup
    parent: cloud-vpcs
    weight: 20
isTocNested: true
showAsideToc: true
---

To create a VPC network, you need to do the following steps:

1. [Create a VPC](../cloud-add-vpc/#create-a-vpc/).

    - The VPC reserves a [range of IP addresses](#setting-the-cidr-and-sizing-your-vpc) for the network.

    - The status of the VPC is Active when done.

1. [Deploy a cluster in the VPC](../cloud-add-vpc/#deploy-a-cluster-in-the-vpc/).

    - You can deploy clusters in a VPC once its status is Active; a peering connection is not required.

1. [Create a peering connection](../cloud-add-peering/#create-a-peering-connection/) between the VPC and the application VPC on the cloud provider network.

    - The status of the peering connection is Pending when done.

1. [Configure your cloud provider](../cloud-add-peering/#configure-the-cloud-provider/) to confirm the connection.

    - In GCP, you Create a peering connection.
    - In AWS, you accept the peering request.
    - The status of the peering connection changes to Active once communication is established.

1. [Add the application VPC CIDR to the cluster IP allow list](../cloud-add-peering/#configure-the-cloud-provider/).

    - To communicate with a cluster, networks must be added to the IP allow list.

With the exception of step 4, these steps are performed in Yugabyte Cloud. For detailed steps, refer to [Set up a VPC network](../cloud-vpc-setup).

VPCs are configured on the [VPCs](../cloud-add-vpc/) page of the **VPC Network** tab on the **Network Access** page.

Peering connections are configured on the [Peering Connections](../cloud-add-peering) page of the **VPC Network** tab on the **Network Access** page.

## Before you begin

Before setting up the VPC network, you will need the following:

- The CIDR block you want to use for your VPC.
- The details of the VPC you want to peer with, including

  - GCP - the project ID and the network name, and CIDR block.
  - AWS - the AWS account ID, and the VPC ID, region, and CIDR block.


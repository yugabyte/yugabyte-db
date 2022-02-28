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

## Before you begin

Before setting up the VPC network, you'll need the following:

- The CIDR block you want to use for your VPC.

  - Refer to [Setting the CIDR and sizing your VPC](../cloud-vpc-intro/#setting-the-cidr-and-sizing-your-vpc).

- The details of the application VPC you want to peer with.

  - AWS - the AWS account ID, and the VPC ID, region, and CIDR block. To obtain these details, navigate to your AWS [Your VPCs](https://console.aws.amazon.com/vpc/home?#vpcs) page for the region where the VPC is located.

  - GCP - the project ID and the network name, and CIDR block. To obtain these details, navigate to your GCP [VPC networks](https://console.cloud.google.com/networking/networks) page.

## Tasks

To create a VPC network, you need to complete the following tasks:

1. [Create a VPC](../cloud-add-vpc/#create-a-vpc).

    - The VPC reserves a range of IP addresses for the network. The range can't overlap with the range used by any application VPC you want to peer.

    - VPCs are configured on the [VPCs](../cloud-add-vpc/) page of the **VPC Network** tab on the **Network Access** page.

    - The status of the VPC is _Active_ when done.

    - Once the VPC is created, you can [deploy a cluster in the VPC](../cloud-add-vpc/#deploy-a-cluster-in-a-vpc); you don't need to wait until the VPC is peered.

1. [Create a peering connection](../cloud-add-peering/) between the VPC and the application VPC on the cloud provider network.

    - Peering connections are configured on the [Peering Connections](../cloud-add-peering) page of the **VPC Network** tab on the **Network Access** page.

    - The status of the peering connection is _Pending_ when done; to make the connection active, you must configure your cloud provider.

1. Configure your cloud provider to confirm the connection.

    - In AWS, [accept the peering request](../cloud-add-peering/#peer-aws).
    - In GCP, [create a peering connection](../cloud-add-peering/#peer-gcp).
    - The status of the peering connection changes to _Active_ once communication is established.

1. [Add the application VPC CIDR to the cluster IP allow list](../../add-connections/).

    - To communicate with a cluster, networks must be added to the cluster IP allow list.

With the exception of 3, these tasks are performed in Yugabyte Cloud.

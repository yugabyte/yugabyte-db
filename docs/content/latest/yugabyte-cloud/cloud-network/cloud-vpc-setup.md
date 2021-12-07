---
title: Set up a VPC network
headerTitle: 
linkTitle: Set up a VPC network
description: How to set up and VPC network in Yugabyte Cloud.
menu:
  latest:
    identifier: cloud-vpc-setup
    parent: cloud-network
    weight: 20
isTocNested: true
showAsideToc: true
---

To set up a VPC network in Yugabyte Cloud, you first create a VPC to host your clusters, then peer it with the VPC that hosts client applications. Some configuration of your cloud provider settings is also required to make the peering connection active. Any cluster deployed using that VPC will then be able to connect with any application in the application VPC.

VPCs are configured on the [VPCs](../cloud-add-vpc/) page of the **VPC Network** tab on the **Network Access** page.

Peering connections are configured on the [Peering Connections](../cloud-add-peering) page of the **VPC Network** tab on the **Network Access** page.

## Before you begin

Before setting up the VPC network, you will need the following:

- The details of the VPN you want to peer with, including

  - GCP - the project ID and the network name, and CIDR block.
  - AWS - the AWS account ID, and the VPC ID, region, and CIDR block.

## Create a VPC in Yugabyte Cloud

1. On the **Network Access** page, select **VPC Network**, then **VPCs**.
1. Cick **Create VPC** to display the **Create VPC** sheet.
1. Enter a name for the VPC.
1. Choose the provider (AWS or GCP).
1. If you selected AWS, select the region. VPCs for GCP are created globally and assigned to all regions supported by Yugabyte Cloud.
1. Specify the VPC CIDR address. You can use the range suggested by Yugabyte Cloud, or enter a custom IP range. The IP range cannot overlap with another VPC in your cloud.
1. Click **Save**.

## Deploy a cluster in the VPC

Once a VPC has been created, you can [deploy your cluster](../../cloud-basics/create-clusters/) in the VPN.

1. On the **Clusters** page, click **Add Cluster**.
1. Choose **Yugabyte Cloud** and click **Next**.
1. Choose the provider you used for your VPC.
1. Enter a name for the cluster.
1. Select the **Region**. For AWS, choose the same region as your VPC.
1. Set the Fault Tolerance. For production clusters, typically this will be Availability Zone Level.
1. Under **Network Access**, choose **Deploy this cluster in a dedicated VPC**, and select your VPC.
1. Click **Create Cluster**.

## Create a Peering Connection in Yugabyte Cloud

Once you have created at least one VPC in Yugabyte Cloud, you can create a peering connection with an application VPC on the same cloud provider. You need to specify the following:

- The Yugabyte Cloud VPC to peer.
- Details of the VPC you want to peer with, including:
  - GCP - the project ID and the network name.
  - AWS - the AWS account ID, and the VPC ID, region, and CIDR block.

To create a peering connection, do the following:

1. On the **Network Access** page, select **VPC Network**, then **Peering Connections**.
1. Click **Create Peering** to display the **Create Peering** sheet.
1. Enter a name for the peering connection.
1. Choose the provider you used for your VPC.
1. Choose the Yugabyte Cloud VPC. Only VPCs that use the same provider are listed.
1. Enter the application VPC information for the provider you selected.
1. Select **Add application CIDR to IP allow list** to add the the CIDR range to your cloud IP allow list. You will add this IP allow list to your cluster so that the application VPC can connect to the database.
1. Click **Initiate Peering**.

## Configure the connection in your cloud provider

Once the peering connection is added in Yugabyte Cloud, you need to sign in to your cloud provider and configure the connection.

- For AWS, use the VPC Dashboard to accept the peering request, enable DNS, and add a route table entry.
- In the Google Cloud Console, create a peering connection using the project ID and network name of the Yugabyte Cloud VPC. 

### Accept the peering request in AWS

To make an AWS peering connection active, in AWS, use the **VPC Dashboard** to do the following:

1. Enable DNS hostnames and DNS resolution. This ensures that the cluster's hostnames in standard connection strings automatically resolve to private instead of public IP addresses when the Yugabyte Cloud cluster is accessed from the application VPC.
1. Approve the peering connection request that you received from Yugabyte.
1. Add a route table entry to the VPC peer and add the Yugabyte Cloud cluster CIDR block to the **Destination** column, and the Peering Connection ID to the **Target** column.

For information on VPC network peering in AWS, refer to [VPC Peering](https://docs.aws.amazon.com/vpc/latest/userguide/vpc-peering.html.) in the AWS documentation.

### Create a peering connection in GCP

To make a GCP peering connection active, you must create a peering connection in GCP. You will need the the **Project ID** and **VPC network name** of the Yugabyte Cloud VPC you are peering with. You can view and copy these details in the [Peering Details](#view-peering-connection-details) sheet.

In the Google Cloud Console, do the following:

1. Navigate to **VPC Network > VPC network peering**.
1. Click **Create Peering Connection**, then click **Continue**.
1. Enter a name for the GCP peering connection.
1. Select your VPC network name.
1. Select **In another project** and enter the **Project ID** and **VPC network name** of the Yugabyte Cloud VPC you are peering with.
1. Click **Create**.

For information on VPC network peering in GCP, refer to [VPC Network Peering overview](https://cloud.google.com/vpc/docs/vpc-peering.) in the Google VPC documentation.

## Add the peered application VPC to the IP allow list for your cluster

Once the cluster and the peering connection are active, you need add at least one of the CIDR blocks associated with the peered application VPC to the [IP allow list](../../cloud-basics/add-connections/) for your cluster.

1. On the **Clusters** page, select your cluster.
1. Click **Quick Links** and **Edit IP Allow List**.
1. Click **Create New List and Add to Cluster**.
1. Enter a name for the allow list.
1. Enter the IP addresses or CIDR.
1. Click **Save**.

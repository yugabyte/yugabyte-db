---
title: Manage peering connections
headerTitle: 
linkTitle: Manage peering connections
description: Manage peering connections to your cloud VPCs.
menu:
  latest:
    identifier: cloud-add-peering
    parent: vpc-peers
    weight: 20
isTocNested: false
showAsideToc: true
---

A peering connection connects a Yugabyte Cloud VPC with a VPC on the corresponding cloud provider - typically one that hosts an application that you want to have access to your cluster. Before you can peer a VPC, you must have an [active VPC configured](../cloud-add-vpc/) for the cloud provider hosting the application VPC.

**Peering Connections** on the **VPC Network** tab displays a list of peering connections configured for your cloud that includes the peering connection name, cloud provider, the network name (GCP) or VPC ID (AWS) of the peered VPC, the name of the Yugabyte VPC, and status of the connection.

![Peering connections](/images/yb-cloud/cloud-vpc-peering.png)

## Create a peering connection

Before you can create a peering connection, you will need the details for the application VPC you will be peering with. The details will depend on the cloud provider, as detailed in the following table.

| AWS | GCP |
| --- | --- |
| AWS account ID | GCP project ID |
| VPC ID | VPC name |
| VPC region | |
| VPC CIDR address | VPC CIDR address (optional) |

You must also have created at least one VPC in Yugabyte Cloud that uses the cloud provider you will be peering with.

To create a peering connection for AWS, do the following:

1. On the **Network Access** page, select **VPC Network**, then **Peering Connections**.
1. Click **Create Peering** to display the **Create Peering** sheet.
1. Enter a name for the peering connection.
1. Choose the provider.
1. Choose the Yugabyte Cloud VPC. Only VPCs that use the same provider are listed.
1. Enter the application VPC information for the provider you selected.
1. Select **Add application CIDR to IP allow list** to add the the CIDR range to your cloud IP allow list. You will add this IP allow list to your cluster so that the application VPC can connect to the database.
1. Click **Initiate Peering**.

The peering connection is created with a status of Pending. To complete the peering, you must accept the peering request in your cloud provider account.

## Configure the cloud provider

Once the peering connection is set up, you need to sign in to your cloud provider and configure the connection.

### Accept the peering request in AWS

Once the peering request is made, in AWS, use the VPC Dashboard to do the following:

1. Enable DNS hostnames and DNS resolution. This ensures that the cluster's hostnames in standard connection strings automatically resolve to private instead of public IP addresses when the Yugabyte Cloud cluster is accessed from the application VPC.
1. Approve the peering connection request that you received from Yugabyte.
1. Add a route table entry to the VPC peer and add the Yugabyte Cloud cluster CIDR block to the Destination column, and the Peering Connection ID to the Target column.

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

## View peering connection details

The **Peering Details** displays information about the peering connection, including both the Yugabyte Cloud VPC details and the peered VPC details.

To view peering connection details:

1. On the **Network Access** page, select **VPC Network**, then **Peering Connections**.
1. Select a peering connection in the list to display the **Peering Details** sheet.

## Terminate a peering connection

To terminate a peering connection, click the **Delete** icon for the peering connection in the list you want to terminate, then click **Terminate**. You can also terminate a peering connection by clicking **Terminate Peering** in the **Peering Details** sheet.

## Next steps

- Create a cluster in a VPN. You do this by selecting the VPC during cluster creation. Refer to [Create a cluster](../../../cloud-basics/create-clusters).
- Add the peered VPN to the IP allow list of your cluster. To communicate with you cluster, the CIDR block of the application VPN must be added to your cluster's IP allow list. Refer to [Assign IP allow lists](../../../cloud-basics/add-connections).

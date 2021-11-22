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

The **Peering Connections** tab displays a list of peering connections configured for your cloud that includes the peering connection name, cloud provider, the name or ID of the peered VPC, the name of the Yugabyte VPC, and status of the connection.

![Create VPC peer](/images/yb-cloud/cloud-networking-vpc.png)

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

1. On the **Peering Connections** tab, click **Create Peering** to display the **Create Peering** sheet.
1. Enter a name for the peering connection.
1. Choose the provider.
1. Choose the Yugabyte Cloud VPC. Only VPCs that use the same provider are listed.
1. Enter the application VPC information for the provider you selected.
1. Select **Add application CIDR to IP allow list** to add the the CIDR range to your cloud IP allow list. You will add this IP allow list to your cluster so that the application VPC can connect to the database.
1. Click **Initiate Peering**.

The peering connection is created with a status of Pending. To complete the peering, you must accept the peering request in you cloud provider account.

### Accept the peering request in AWS

Once the peering request is made, in AWS, use the VPC Dashboard to do the following:

1. Enable DNS hostnames and DNS resolution. This ensures that the cluster's hostnames in standard connection strings automatically resolve to private instead of public IP addresses when the Yugabyte Cloud cluster is accessed from the application VPC.
1. Approve the peering connection request that you received from Yugabyte.
1. Add a route table entry to the VPC peer and add the Yugabyte Cloud cluster CIDR block to the Destination column, and the Peering Connection ID to the Target column.

### Accept the peering request in GCP

Once the peering request is made, in the Google Cloud Console, create a peering connection using the project ID and VPC network name.

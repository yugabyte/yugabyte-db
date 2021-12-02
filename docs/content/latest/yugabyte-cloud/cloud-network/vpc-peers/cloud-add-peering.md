---
title: Manage peering connections
headerTitle: 
linkTitle: Peering Connections
description: Manage peering connections to your cloud VPCs.
menu:
  latest:
    identifier: cloud-add-peering
    parent: vpc-peers
    weight: 40
isTocNested: true
showAsideToc: true
---

A peering connection connects a Yugabyte Cloud VPC with a VPC on the corresponding cloud provider - typically one that hosts an application that you want to have access to your cluster. Before you can peer a VPC, you must have an [active VPC configured](../cloud-add-vpc/) for the cloud provider hosting the application VPC.

**Peering Connections** on the **VPC Network** tab displays a list of peering connections configured for your cloud that includes the peering connection name, cloud provider, the network name (GCP) or VPC ID (AWS) of the peered VPC, the name of the Yugabyte VPC, and status of the connection (Pending or Active).

![Peering connections](/images/yb-cloud/cloud-vpc-peering.png)

To view the peering connection details, select a peering connection in the list to display the **Peering Details** sheet.

{{< note title="Note" >}}

If you have an _Active_ peering connection but are unable to connect to a cluster in the VPC, ensure that you have added the CIDR block of the peered application VPN to your cluster's IP allow list. For information on adding IP allow lists, refer to [Assign IP allow lists](../../../cloud-basics/add-connections).

{{< /note >}}

## Configure a peering connection

Configuring a peering connection is done in two stages:

1. [Create the peering connection in Yugabyte Cloud](#create-a-peering-connection). You must have already created a VPC in Yugabyte Cloud, and you will need the details of the application VPC you want to peer with. When this is done, the peering connection is listed in Yuagbyte Cloud with a status of _Pending_.
2. [Configure the peering in your cloud provider](#configure-the-cloud-provider).
    - In AWS, this requires accepting the peering request.
    - In GCP, this requires creating a peering connection.

If successful, the peering connection status will change to _Active_. To communicate with your cluster, you still need to add the peered VPN to the IP allow list of your cluster.

### Create a peering connection

Before you can create a peering connection, you must have created at least one VPC in Yugabyte Cloud that uses the cloud provider you will be peering with. In addition, you will need the following details for the application VPC you will be peering with.

| Provider | VPC Details |
| --- | --- |
| AWS | Account ID<br>VPC ID<br>VPC region<br>VPC CIDR address |
| GCP | GCP project ID<br>VPC name<br>VPC CIDR address (optional) |

To create a peering connection, do the following:

1. On the **Network Access** page, select **VPC Network**, then **Peering Connections**.
1. Click **Create Peering** to display the **Create Peering** sheet.
1. Enter a name for the peering connection.
1. Choose the provider.
1. Choose the Yugabyte Cloud VPC. Only VPCs that use the same provider are listed.
1. Enter the application VPC information for the provider you selected.
1. Select **Add application CIDR to IP allow list** to add the the CIDR range to your cloud IP allow list. You will add this IP allow list to your cluster so that the application VPC can connect to the database.
1. Click **Initiate Peering**.

The peering connection is created with a status of _Pending_. To complete the peering, you must accept the peering request in your cloud provider account.

### Configure the cloud provider

To complete a _Pending_ peering connection, you need to sign in to your cloud provider and either accept the peering request (AWS), or create a peering connection (GCP).

#### Accept the peering request in AWS

To make an AWS peering connection active, in AWS, use the **VPC Dashboard** to do the following:

1. Enable DNS hostnames and DNS resolution. This ensures that the cluster's hostnames in standard connection strings automatically resolve to private instead of public IP addresses when the Yugabyte Cloud cluster is accessed from the application VPC.
1. Approve the peering connection request that you received from Yugabyte.
1. Add a route table entry to the VPC peer and add the Yugabyte Cloud cluster CIDR block to the **Destination** column, and the Peering Connection ID to the **Target** column.

For information on VPC network peering in AWS, refer to [VPC Peering](https://docs.aws.amazon.com/vpc/latest/userguide/vpc-peering.html.) in the AWS documentation.

#### Create a peering connection in GCP

To make a GCP peering connection active, you must create a peering connection in GCP. You will need the the **Project ID** and **VPC network name** of the Yugabyte Cloud VPC you are peering with. You can view and copy these details in the [Peering Details](#view-peering-connection-details) sheet.

In the Google Cloud Console, do the following:

1. Navigate to **VPC Network > VPC network peering**.
1. Click **Create Peering Connection**, then click **Continue**.
1. Enter a name for the GCP peering connection.
1. Select your VPC network name.
1. Select **In another project** and enter the **Project ID** and **VPC network name** of the Yugabyte Cloud VPC you are peering with.
1. Click **Create**.

For information on VPC network peering in GCP, refer to [VPC Network Peering overview](https://cloud.google.com/vpc/docs/vpc-peering.) in the Google VPC documentation.

## Terminate a peering connection

To terminate a peering connection, click the **Delete** icon for the peering connection in the list you want to terminate, then click **Terminate**. You can also terminate a peering connection by clicking **Terminate Peering** in the **Peering Details** sheet.

## Related topics

- Create a cluster in a VPN. You do this by selecting the VPC during cluster creation. Refer to [Create a cluster](../../../cloud-basics/create-clusters).
- Add the peered VPN to the IP allow list of your cluster. To communicate with your cluster, the CIDR block of the application VPN must be added to your cluster's IP allow list. Refer to [Assign IP allow lists](../../../cloud-basics/add-connections).

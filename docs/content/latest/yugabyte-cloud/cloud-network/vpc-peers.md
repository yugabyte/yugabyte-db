---
title: VPC peering
linkTitle: VPC peering
description: Manage Yugabyte Cloud VPC peers.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: vpc-peers
    parent: cloud-network
    weight: 200
isTocNested: true
showAsideToc: true
---

<!--
The **VPC Peering** tab displays a list of peers configured for your cloud that includes the VPC name, provider, region, ID, CIDR, local VPC IP address, and cluster to which the peer is assigned.

![Cloud Network VPC Peering page](/images/yb-cloud/cloud-networking-vpc.png)
-->

Virtual Private Cloud (VPC) peering allows applications running on instances on the same cloud provider as your Yugabyte Cloud cluster to communicate with your YugabyteDB clusters without traversing the public internet - traffic stays within the cloud provider's network.

{{< note title="Note" >}}

To use VPC peering in a cluster, your cluster must be deployed in a dedicated VPC that is peered with your application VPC. You must set up dedicated VPCs and peering before deploying your cluster. VPC peering is only supported in Paid clusters.

VPC peering is set up by Yugabyte. Self service VPC Peering is in development and will be available in the future.

{{< /note >}}

## What to send to Yugabyte Support

To set up a VPC peer, contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431) with the following information:

- Cloud provider of choice
- Region
- Preferred CIDR to use for your database VPC.
- Details of the VPC that you want to with peer with, including
  - AWS account or GCP project
  - VPC ID/network name
  - CIDR blocks of the VPC network

## Configuring the VPC peer connection

Once Support creates the Yugabyte Cloud cluster and database, you will be contacted with the following information:

- The credentials for your YugabyteDB database.
- The connection endpoints (if you are using YSQL this is not needed).
- VPC peering details, including:
  - GCP - the project ID and the network name that you need to peer to.
  - AWS - a Peering Connection ID (this will also be displayed in your AWS console) and the CIDR block for your Yugabyte Cloud cluster. You will also receive a peering connection request.

Use this information to configure your VPC so that it can connect to the network where the YugabyteDB database has been provisioned.

### GCP

In the Google Cloud Console, create a peering connection using the project ID and VPC network name.

### AWS

Use the VPC Dashboard to do the following:

- Enable DNS hostnames and DNS resolution. This ensures that the cluster's hostnames in standard connection strings automatically resolve to private instead of public IP addresses when the Yugabyte Cloud cluster is accessed from the VPC.
- Approve the peering connection request that you received from Yugabyte.
- Add a route table entry to the VPC peer and add the Yugabyte Cloud cluster CIDR block to the Destination column, and the Peering Connection ID to the Target column.

### Yugabyte Cloud

Before your VPC peer can connect to your cluster, you must:

1. Locate the VPC CIDR block addresses (or subset) associated with the VPC for your cloud provider.
1. Add at least one of these CIDR blocks to the [IP allow list](../../cloud-basics/add-connections) for your Yugabyte Cloud cluster.

<!--
## Add VPC peers

To add a VPC peer:

1. On the **VPC Peering** tab, click **Add Peer** to display the **Add VPC Peer** sheet.
1. Enter a name for the peer.
1. Choose the provider and enter your account ID and VPC ID.
1. Select the region.
1. Enter the VPC CIDR address, and select the **Add this CIDR block to my IP allowlist** option.
1. Set the **Status** option to **Enabled**.

## Edit VPC peers

To edit a VPC peer:

1. On the **VPC Peering** tab, select a peer and click the **Edit** icon to display the **Edit VPC Peer** sheet.
1. Update the name and subnet IDs.
-->

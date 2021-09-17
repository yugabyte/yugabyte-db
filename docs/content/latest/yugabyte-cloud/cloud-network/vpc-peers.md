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

Virtual Private Cloud (VPC) peering allows applications running on other cloud instances to communicate with your YugabyteDB clusters without traversing the public internet - traffic stays within the cloud provider's network. VPC peering allows internal IP address connectivity across two VPC networks regardless of whether they belong to the same project or the same organization.

{{< note title="Note" >}}

To use VPC peering in a cluster, your cluster must be deployed in a dedicated VPC that is peered with your application VPC. You must set up dedicated VPCs and peering before deploying your cluster. VPC peering is only supported in Paid clusters.

VPC peering is set up by Yugabyte. Self service VPC Peering is in development and will be available in the future.

{{< /note >}}

To set up a VPC peer, contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431) with the following information:

- Cloud provider of choice
- Region
- Preferred CIDR to use for your database VPC.
- The VPCs on your end that you want to with peer with, including
  - AWS account or GCP project
  - VPC ID/network name
  - CIDR blocks of the VPC/network

Once Support creates the Yugabyte Cloud cluster and database, you will be contacted with the following information:

- The credentials for your YugabyteDB database.
- The connection endpoints (if you are using YSQL this is not needed).
- VPC peering details, including:
  - For GCP, the project and the network name that you need to peer to, so that you can create a peer on your end.
  - For AWS, a peering ID; this will also be displayed in your AWS console. You also need to create a routing table entry for your subnets/VPC to enable connectivity between your network and Yugabyte Cloud. 

Use this information to configure your VPC so that it can connect to the network where the YugabyteDB database has been provisioned. 

Finally, [add an IP allow list](../../cloud-basics/add-connections) for your Yugabyte Cloud cluster to enable certain instances or the whole CIDR range of your network (or particular subnets) to create the ingress rules to allow connections.

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

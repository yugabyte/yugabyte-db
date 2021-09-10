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

To use VPC peering in a cluster, your cluster must be deployed in a dedicated VPC that is peered with your application VPC. Contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431) to set up dedicated VPCs and peering before deploying your cluster. VPC peering is only supported in Paid clusters.

Self service VPC Peering is in development and will be available in the future.

{{< /note >}}

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

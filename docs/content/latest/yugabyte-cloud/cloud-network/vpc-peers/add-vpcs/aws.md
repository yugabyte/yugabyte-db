---
title: Add a VPC peer for AWS
headerTitle: Add a VPC peer for AWS
linkTitle: Add VPC peers
description: Add a VPC peer for AWS.
menu:
  latest:
    identifier: add-vpc-1-aws
    parent: vpc-peers
    weight: 10
isTocNested: false
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../aws/" class="nav-link active">
      <i class="fab fa-aws"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../gcp/" class="nav-link">
      <i class="fab fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

</ul>

The **VPC Peering** tab displays a list of peers configured for your cloud that includes the VPC name, provider, region, ID, CIDR, local VPC IP address, and cluster to which the peer is assigned.

![Create VPC peer](/images/yb-cloud/cloud-networking-vpc.png)

To create a VPC peer on AWS, do the following:

1. On the **VPCs** tab, click **Create VPC** to display the **Add VPC Peer** sheet.
1. Enter a name and, optionally, a description for the VPC.
1. Choose the provider and enter your account ID and VPC ID.
1. Select the region.
1. Specify the VPC CIDR address. You can use the range suggested by Yugabyte Cloud, or enter a custom IP range. The IP range cannot overlap with another VPC in your cloud.
1. Click **Save**.

## Edit VPC peers

To edit a VPC peer:

1. On the **VPC Peering** tab, select a peer and click the **Edit** icon to display the **Edit VPC Peer** sheet.
1. Update the name and subnet IDs.

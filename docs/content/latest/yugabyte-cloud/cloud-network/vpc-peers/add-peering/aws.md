---
title: Add a peering connection for AWS
headerTitle: Add a peering connection for AWS
linkTitle: Add peering connections
description: Add a peering connection for AWS.
menu:
  latest:
    identifier: add-peering-1-aws
    parent: vpc-peers
    weight: 20
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

The **Peering Connections** tab displays a list of peering connections configured for your cloud that includes the VPC name, provider, region, ID, CIDR, local VPC IP address, and cluster to which the peer is assigned.

![Create VPC peer](/images/yb-cloud/cloud-networking-vpc.png)

To create a peering connection on AWS, do the following:

1. On the **Peering Connections** tab, click **Create Peering** to display the **Create Peering** sheet.
1. Enter a name for the peering connection.
1. Choose the provider.
1. Choose the Yugabyte Cloud VPC.
1. Enter your AWS account ID and VPC ID.
1. Select the client VPC region.
1. Enter the client VPC CIDR address. The IP range cannot overlap with the Yugabyte Cloud VPC IP range.
1. Click **Initiate Peering**.

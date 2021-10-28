---
title: Add a peering connection for GCP
headerTitle: Add a peering connection for GCP
linkTitle: Add peering connections
description: Add a peering connection for GCP.
menu:
  latest:
    identifier: add-peering-2-gcp
    parent: vpc-peers
    weight: 20
isTocNested: false
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../aws/" class="nav-link">
      <i class="fab fa-aws"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../gcp/" class="nav-link active">
      <i class="fab fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

</ul>

The **Peering Connections** tab displays a list of peering connections configured for your cloud that includes the VPC name, provider, region, ID, CIDR, local VPC IP address, and cluster to which the peer is assigned.

![Create VPC peer](/images/yb-cloud/cloud-networking-vpc.png)

To create a peering connection on GCP, do the following:

1. On the **Peering Connections** tab, click **Create Peering** to display the **Create Peering** sheet.
1. Enter a name for the peering connection.
1. Choose the provider.
1. Choose the Yugabyte Cloud VPC.
1. Enter your GCP project ID.
1. Enter the VPC name.
1. Optionally, enter the VPC CIDR address.
1. Click **Initiate Peering**.

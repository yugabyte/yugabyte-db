---
title: Manage VPCs
headerTitle: 
linkTitle: Manage VPCs
description: Manage your cloud VPCs.
menu:
  latest:
    identifier: cloud-add-vpc
    parent: vpc-peers
    weight: 10
isTocNested: false
showAsideToc: true
---

The **VPCs** tab displays a list of VPCs configured for your cloud that includes the VPC name, provider, region, ID, CIDR, local VPC IP address, and cluster to which the VPC is assigned.

![VPCs](/images/yb-cloud/cloud-networking-vpc.png)

## Create a VPC

To create a VPC, do the following:

1. On the **Network Access** page, select **VPCs**.
1. Cick **Create VPC** to display the **Create VPC** sheet.
1. Enter a name for the VPC.
1. Choose the provider (AWS or GCP).
1. If you selected AWS, select the region.
1. Specify the VPC CIDR address. You can use the range suggested by Yugabyte Cloud, or enter a custom IP range. The IP range cannot overlap with another VPC in your cloud.
1. Click **Save**.

## Edit a VPC

To edit a VPC peer:

1. On the **Network Access** page, select **VPCs**.
1. Select a peer and click the **Edit** icon to display the **Edit VPC** sheet.
1. Update the name and subnet IDs.

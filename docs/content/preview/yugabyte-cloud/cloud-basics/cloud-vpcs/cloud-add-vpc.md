---
title: VPCs
headerTitle:
linkTitle: VPCs
description: Manage your cloud VPCs.
menu:
  preview:
    identifier: cloud-add-vpc
    parent: cloud-vpcs
    weight: 30
isTocNested: true
showAsideToc: true
---

A virtual private cloud (VPC) is a virtual network where you can deploy clusters that you want to peer with application VPCs hosted with the same provider. The VPC reserves a range of IP addresses with the cloud provider you select. You must set up a dedicated VPC before deploying your cluster. A VPC must be created before you can configure a peering connection.

**VPCs** on the **VPC Network** tab displays a list of VPCs configured for your cloud that includes the VPC name, provider, region, CIDR, number of peerings, number of clusters deployed in the VPC, and status.

![VPCs](/images/yb-cloud/cloud-vpc.png)

To view VPC details, select a VPC in the list to display the **VPC Details** sheet.

To terminate a VPC, click the **Delete** icon for the VPC in the list you want to terminate, and then click **Terminate**. You can also terminate a VPC by clicking **Terminate VPC** in the **VPC Details** sheet. You can't terminate a VPC with active peering connections or clusters.

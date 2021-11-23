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

To set up peering in Yugabyte Cloud, you first create a VPC to host your clusters. The VPC reserves a range of IP addresses with the cloud provider you select. A VPC must be created before you can configure a peering connection. You must set up a dedicated VPC and peering before deploying your cluster.

The **VPCs** tab displays a list of VPCs configured for your cloud that includes the VPC name, provider, region, ID, CIDR, local VPC IP address, and cluster to which the VPC is assigned.

![VPCs](/images/yb-cloud/cloud-networking-vpc.png)

## Create a VPC

To create a VPC, do the following:

1. On the **Network Access** page, select **VPC Network**, then **VPCs**.
1. Cick **Create VPC** to display the **Create VPC** sheet.
1. Enter a name for the VPC.
1. Choose the provider (AWS or GCP).
1. If you selected AWS, select the region. VPCs for GCP are created globally and assigned to all regions supported by Yugabyte Cloud.
1. Specify the VPC CIDR address. You can use the range suggested by Yugabyte Cloud, or enter a custom IP range. The IP range cannot overlap with another VPC in your cloud.
1. Click **Save**.

Yugabyte Cloud adds the VPC to the VPCs list with a status of Creating. If successful, after a minute or two, the status will change to Active.

The VPC network name 

## View VPC details

To view VPC details:

1. On the **Network Access** page, select **VPCs**.
1. Select a VPC in the list to display the **VPC Detail** sheet.

## Terminate a VPC

To terminate a VPC, click the **Delete** icon for the VPC in the list you want to terminate, then click **Terminate**. You can also terminate a VPC by clicking **Terminate VPC** in the **VPC Details** sheet.

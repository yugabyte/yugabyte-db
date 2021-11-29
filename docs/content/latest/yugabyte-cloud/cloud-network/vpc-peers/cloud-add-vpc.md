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
isTocNested: true
showAsideToc: true
---

To set up peering in Yugabyte Cloud, you first create a VPC to host your clusters. The VPC reserves a range of IP addresses with the cloud provider you select. A VPC must be created before you can configure a peering connection. You must set up a dedicated VPC before deploying your cluster.

**VPCs** on the **VPC Network** tab displays a list of VPCs configured for your cloud that includes the VPC name, provider, region, ID, CIDR, local VPC IP address, and cluster to which the VPC is assigned.

![VPCs](/images/yb-cloud/cloud-vpc.png)

## Create a VPC

To create a VPC in Yugabyte Cloud, you need to specify the following:

- Cloud provider (AWS or GCP).
- Region in which to deploy the VPC (AWS only). Typically, you will want this to be the same region as the application VPC you want to peer with. In GCP, the VPC is deployed in all regions.
- Preferred CIDR to use for your database VPC.

To create a VPC, do the following:

1. On the **Network Access** page, select **VPC Network**, then **VPCs**.
1. Cick **Create VPC** to display the **Create VPC** sheet.
1. Enter a name for the VPC.
1. Choose the provider (AWS or GCP).
1. If you selected AWS, select the region. VPCs for GCP are created globally and assigned to all regions supported by Yugabyte Cloud.
1. Specify the VPC CIDR address. You can use the range suggested by Yugabyte Cloud, or enter a custom IP range. The IP range cannot overlap with another VPC in your cloud.
1. Click **Save**.

Yugabyte Cloud adds the VPC to the VPCs list with a status of Creating. If successful, after a minute or two, the status will change to Active.

The VPC's network name and project ID are automatically assigned. You will need these details when configuring the peering in GCP.

## View VPC details

The VPC details include the following:

- Provider hosting the VPC
- Status
- Name
- CIDR
- Network Name
- Region
- Project ID

To view the VPC details:

1. On the **Network Access** page, select **VPCs**.
1. Select a VPC in the list to display the **VPC Details** sheet.

## Terminate a VPC

To terminate a VPC, click the **Delete** icon for the VPC in the list you want to terminate, then click **Terminate**. You can also terminate a VPC by clicking **Terminate VPC** in the **VPC Details** sheet.

## Next steps

- Create a cluster in a VPN. You do this by selecting the VPC during cluster creation. Refer to [Create a cluster](../../../cloud-basics/create-clusters).
- Connect a Yugabyte Cloud VPN to an application VPN. Refer to [Add a peering connection](../cloud-add-peering).

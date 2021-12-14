---
title: Create VPCs
headerTitle: 
linkTitle: VPCs
description: Create and manage your cloud VPCs.
menu:
  latest:
    identifier: cloud-add-vpc
    parent: cloud-vpcs
    weight: 30
isTocNested: true
showAsideToc: true
---

To set up a VPC network in Yugabyte Cloud, you first create a VPC to host your clusters. The VPC reserves a range of IP addresses with the cloud provider you select. A VPC must be created before you can configure a peering connection. You must set up a dedicated VPC before deploying your cluster.

**VPCs** on the **VPC Network** tab displays a list of VPCs configured for your cloud that includes the VPC name, provider, region, ID, CIDR, local VPC IP address, and cluster to which the VPC is assigned.

![VPCs](/images/yb-cloud/cloud-vpc.png)

To view VPC details, select a VPC in the list to display the **VPC Details** sheet.

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
1. Specify the VPC CIDR address. You can use the range suggested by Yugabyte Cloud, or enter a custom IP range. The IP range cannot overlap with another VPC in your cloud. Refer to [Sizing your VPC](../cloud-vpc-intro/#sizing-your-vpc/).
1. Click **Save**.

Yugabyte Cloud adds the VPC to the VPCs list with a status of Creating. If successful, after a minute or two, the status will change to Active.

The VPC's network name and project ID are automatically assigned. You will need these details when configuring the peering in GCP.

## Deploy a cluster in a VPC

Once a VPC has been created, you can [deploy your cluster](../../cloud-basics/create-clusters/) in the VPC.

1. On the **Clusters** page, click **Add Cluster**.
1. Choose **Yugabyte Cloud** and click **Next**.
1. Choose the provider you used for your VPC.
1. Enter a name for the cluster.
1. Select the **Region**. For AWS, choose the same region as your VPC.
1. Set the Fault Tolerance. For production clusters, typically this will be Availability Zone Level.
1. Under **Network Access**, choose **Deploy this cluster in a dedicated VPC**, and select your VPC.
1. Click **Create Cluster**.

## Terminate a VPC

You cannot terminate a VPC with active peering connections or clusters.

To terminate a VPC, click the **Delete** icon for the VPC in the list you want to terminate, then click **Terminate**. You can also terminate a VPC by clicking **Terminate VPC** in the **VPC Details** sheet.

## Next steps

- [Create a peering connection](../cloud-add-peering/#create-a-peering-connection/).
- [Configure your cloud provider](../cloud-add-peering/#configure-the-cloud-provider/).
- [Add an application VPC to the IP allow list](../cloud-add-peering/#configure-the-cloud-provider/).

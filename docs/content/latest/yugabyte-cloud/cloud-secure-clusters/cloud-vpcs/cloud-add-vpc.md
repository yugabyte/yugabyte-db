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

The first step in setting up a VPC network in Yugabyte Cloud is to create a VPC to host your clusters. The VPC reserves a range of IP addresses with the cloud provider you select. You must set up a dedicated VPC before deploying your cluster. A VPC must be created before you can configure a peering connection.

**VPCs** on the **VPC Network** tab displays a list of VPCs configured for your cloud that includes the VPC name, provider, region, CIDR, number of peerings, number of clusters deployed in the VPC, and status.

![VPCs](/images/yb-cloud/cloud-vpc.png)

To view VPC details, select a VPC in the list to display the **VPC Details** sheet.

To terminate a VPC, click the **Delete** icon for the VPC in the list you want to terminate, and then click **Terminate**. You can also terminate a VPC by clicking **Terminate VPC** in the **VPC Details** sheet. You can't terminate a VPC with active peering connections or clusters.

## Create a VPC

To create a VPC in Yugabyte Cloud, you need to specify the following:

- Cloud provider (AWS or GCP).
- Region in which to deploy the VPC (AWS and GPC custom). For GPC automated, the VPC is deployed in all regions.
- Preferred CIDR to use for your database VPC.

You'll also need to know the CIDR range for the application VPC with which you want to peer, as the addresses can't overlap. To view the application VPC CIDR address:

- For AWS, navigate to the [Your VPCs](https://console.aws.amazon.com/vpc/home?#vpcs) page for the region hosting the VPC you want to peer.
- For GCP, navigate to the [VPC networks](https://console.cloud.google.com/networking/networks) page.

To create a VPC, do the following:

1. On the **Network Access** page, select **VPC Network**, then **VPCs**.
1. Cick **Create VPC** to display the **Create VPC** sheet.
1. Enter a name for the VPC.
1. Choose the provider (AWS or GCP).
1. If you selected **GCP**, choose one of the following options:
    - **Automated** - VPCs are created globally and assigned to all regions supported by Yugabyte Cloud.
    - **Custom** - Select a region and [specify the CIDR address](../cloud-vpc-intro/#setting-the-cidr-and-sizing-your-vpc). Click **Add Region** to add additional regions. CIDR addresses in different regions cannot overlap.
1. If you selected **AWS**, select the region and specify the CIDR address.
1. Click **Save**.

Yugabyte Cloud adds the VPC to the VPCs list with a status of _Creating_. If successful, after a minute or two, the status will change to _Active_.

The VPC's network name and project ID are automatically assigned. You'll need these details when configuring the peering in GCP.

## Deploy a cluster in a VPC

You can [deploy your cluster](../../cloud-basics/create-clusters/) in a VPC any time after it has been created.

1. On the **Clusters** page, click **Add Cluster**.
1. Choose **Yugabyte Cloud** and click **Next**.
1. Choose the provider you used for your VPC.
1. Enter a name for the cluster.
1. Select the **Region**. For AWS or GCP custom, select the region where the VPC is deployed.
1. Set the Fault Tolerance. For production clusters, typically this will be Availability Zone Level.
1. Under **Network Access**, choose **Deploy this cluster in a dedicated VPC**, and select your VPC.
1. Click **Create Cluster**.

For more information on creating clusters, refer to [Create a cluster](../../cloud-basics/create-clusters/).

## Next steps

- [Create a peering connection and configure your cloud provider](../cloud-add-peering/)
- [Add the application VPC CIDR to the cluster IP allow list](../../add-connections/)

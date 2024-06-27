---
title: VPCs
headerTitle:
linkTitle: VPCs
description: Manage your YugabyteDB Aeon VPCs.
headcontent: Manage your YugabyteDB Aeon VPCs
menu:
  preview_yugabyte-cloud:
    identifier: cloud-add-vpc
    parent: cloud-vpcs
    weight: 20
type: docs
---

A virtual private cloud (VPC) is a virtual network where you can deploy clusters that you want to connect with services hosted with the same provider. The VPC reserves a range of IP addresses with the cloud provider you select.

- To learn about VPCs in YugabyteDB Aeon, refer to [VPC overview](../cloud-vpc-intro/).
- To learn how to peer VPCs, refer to [Peering connections](../cloud-add-peering/).
- To learn how to configure a private service endpoint to use with a private link service, refer to [Private service endpoints](../cloud-add-endpoint/).

For lowest latencies, create your VPC in the same region(s) as your applications. If you are connecting to your application via a private service endpoint, your cluster VPC must be located in the same region as the VPC endpoint to which you are linking.

**VPCs** on the **VPC Network** tab of the **Networking** page displays a list of VPCs configured for your cloud that includes the VPC name, provider, region, CIDR, number of peering connections, number of clusters deployed in the VPC, and status.

![VPCs](/images/yb-cloud/cloud-vpc.png)

To view VPC details, select a VPC in the list to display the **VPC Details** sheet.

To terminate a VPC, click the **Delete** icon for the VPC in the list you want to terminate, and then click **Terminate**. You can also terminate a VPC by clicking **Terminate VPC** in the **VPC Details** sheet. You can't terminate a VPC with active peering connections or clusters.

## Create a VPC

To create a VPC, do the following:

{{< tabpane text=true >}}

  {{% tab header="AWS" lang="aws" %}}

1. On the **Networking** page, select **VPC Network**, then **VPCs**.
1. Click **Create VPC** to display the **Create VPC** sheet.
1. Enter a name for the VPC.
1. Choose the provider (AWS).
1. Select the [region](../cloud-vpc-intro/#choose-the-region-for-your-vpc).
1. [Specify the CIDR address](../cloud-vpc-intro/#set-the-cidr-and-size-your-vpc). Ensure the following:
    - the address _does not overlap_ with that of any application VPC you want to peer.
    - the address _does not overlap_ with VPCs that will be used for other regions of a multi-region cluster.
    - for production clusters, use network sizes of /24 or /25.
1. Click **Save**.

  {{% /tab %}}

  {{% tab header="Azure" lang="azure" %}}

1. On the **Networking** page, select **VPC Network**, then **VPCs**.
1. Click **Create VPC** to display the **Create VPC** sheet.
1. Enter a name for the VPC.
1. Choose the provider (Azure).
1. Select the [region](../cloud-vpc-intro/#choose-the-region-for-your-vpc).
1. Click **Save**.

  {{% /tab %}}

  {{% tab header="GCP" lang="gcp" %}}

1. On the **Networking** page, select **VPC Network**, then **VPCs**.
1. Click **Create VPC** to display the **Create VPC** sheet.
1. Enter a name for the VPC.
1. Choose the provider (GCP).
1. Choose one of the following options:
    - **Automated** - VPCs are created globally and GCP assigns network blocks to each region supported by YugabyteDB Aeon. (Not recommended for production, refer to [Considerations for auto mode VPC networks](https://cloud.google.com/vpc/docs/vpc#auto-mode-considerations) in the GCP documentation.)
    - **Custom** - Select a region. Click **Add Region** to add additional regions. If the VPC is to be used for a multi-region cluster, add a region for each of the regions in the cluster.
1. [Specify the CIDR address](../cloud-vpc-intro/#set-the-cidr-and-size-your-vpc). CIDR addresses in different regions can't overlap.
    - For Automated, use network sizes of /16, /17, or /18.
    - For Custom, use network sizes of /24, /25, or /26.

    Ensure the address _does not overlap_ with that of the application VPC.

1. Click **Save**.

  {{% /tab %}}

{{< /tabpane >}}

YugabyteDB Aeon adds the VPC to the VPCs list with a status of _Creating_. If successful, after a minute or two, the status will change to _Active_.

## Limitations

- You assign a VPC when you create a cluster. You can't switch VPCs after cluster creation.
- You can't change the size of your VPC once it is created.
- You can't peer VPCs with overlapping ranges with the same application VPC.
- You can create a maximum of 3 AWS VPCs per region.
- You can create a maximum of 3 GCP VPCs.
- VPCs are not supported on Sandbox clusters.

If you need additional VPCs, contact {{% support-cloud %}}.

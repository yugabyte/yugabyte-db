---
title: Peer VPCs in AWS
headerTitle: Peer VPCs
linkTitle: Peer VPCs
description: Peer a VPC in AWS.
headcontent: Peer your cluster VPC with a VPC in AWS
aliases:
  - /preview/yugabyte-cloud/cloud-basics/cloud-vpcs/cloud-configure-provider/
menu:
  preview_yugabyte-cloud:
    identifier: cloud-add-vpc-1-aws
    parent: cloud-add-peering
    weight: 50
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../cloud-add-vpc-aws/" class="nav-link active">
      <i class="fa-brands fa-aws" aria-hidden="true"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../cloud-add-vpc-gcp/" class="nav-link">
       <i class="fa-brands fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

</ul>

YugabyteDB Managed supports peering virtual private cloud (VPC) networks on AWS and GCP.

Using YugabyteDB Managed, you can create a VPC on AWS, deploy clusters in the VPC, and peer the VPC with application VPCs hosted on AWS.

To peer VPCs that reside in AWS, you need to complete the following tasks:

| Task | Notes |
| :--- | :--- |
| **[Create the VPC](#create-a-vpc)** | Reserves a range of private IP addresses for the network.<br>You need to create a VPC for each region in multi-region clusters.<br>The status of the VPC is _Active_ when done. |
| **[Create a peering connection](#create-a-peering-connection)** | Connects your VPC and the application VPC on the cloud provider network.<br>The status of the peering connection is _Pending_ when done. |
| **[Accept the peering request<br>in AWS](#accept-the-peering-request-in-aws)** | Confirms the connection between your VPC and the application VPC.<br>The status of the peering connection is _Active_ when done. |
| **[Add the route table entry<br>in AWS](#add-the-route-table-entry-in-aws)** | Adds a route to the route table of the application VPC so that you can send and receive traffic across the peering connection. |
| **[Deploy a cluster in the VPC](#deploy-a-cluster-in-the-vpc)** | This can be done at any time - you don't need to wait until the VPC is peered. |
| **[Add the application VPC to the IP allow list](#add-the-application-vpc-to-the-cluster-ip-allow-list)** | Allows the peered application VPC to connect to the cluster.<br>Add at least one of the CIDR blocks associated with the peered application VPC to the [IP allow list](../../../cloud-secure-clusters/add-connections/) for your cluster. |

With the exception of accepting the peering request and adding the route table entry in AWS, these tasks are performed in YugabyteDB Managed.

For information on VPC peering in AWS, refer to [VPC Peering](https://docs.aws.amazon.com/vpc/latest/userguide/vpc-peering.html) in the AWS documentation.

## Create a VPC

To avoid cross-region data transfer costs, deploy your VPC in the same region as the application VPC you are peering with.

If you intend to deploy a multi-region cluster, you need to create a separate VPC for each region.

{{< tip title="What you need" >}}
The CIDR range for the application VPC with which you want to peer, as _the addresses can't overlap_.

**Where to find it**<br>Navigate to the AWS [Your VPCs](https://console.aws.amazon.com/vpc/home?#vpcs) page for the region hosting the VPC you want to peer.
{{< /tip >}}

To create a VPC, do the following:

1. On the **Networking** page, select **VPC Network**, then **VPCs**.
1. Click **Create VPC** to display the **Create VPC** sheet.
1. Enter a name for the VPC.
1. Choose the provider (AWS).
1. Select the [region](../cloud-vpc-intro/#choose-the-region-for-your-vpc). Typically, the same region that hosts the VPC with which you want to peer.
1. [Specify the CIDR address](../cloud-vpc-intro/#set-the-cidr-and-size-your-vpc). Ensure the following:
    - the address _does not overlap_ with that of the application VPC.
    - the address _does not overlap_ with the VPCs that will be used for the other regions of a multi-region cluster.
    - for production clusters, use network sizes of /24 or /25.
1. Click **Save**.

YugabyteDB Managed adds the VPC to the VPCs list with a status of _Creating_. If successful, after a minute or two, the status will change to _Active_.

## Create a peering connection

After creating a VPC in YugabyteDB Managed that uses AWS, you can peer it with an AWS application VPC.

{{< tip title="What you need" >}}
The following details for the AWS application VPC you are peering with:

- Account ID
- VPC ID
- VPC region
- VPC CIDR address

**Where to find it**<br>Navigate to your AWS [Your VPCs](https://console.aws.amazon.com/vpc/home?#vpcs) page for the region hosting the VPC you want to peer.
{{< /tip >}}

To create a peering connection, in YugabyteDB Managed do the following:

1. On the **Networking** page, select **VPC Network**, then **Peering Connections**.
1. Click **Add Peering Connection** to display the **Create Peering** sheet.
1. Enter a name for the peering connection.
1. Choose **AWS**.
1. Choose the YugabyteDB Managed VPC you are peering. Only VPCs that use AWS are listed.
1. Enter the AWS account ID, and the application VPC ID, region, and CIDR address.
1. Click **Initiate Peering**.

The peering connection is created with a status of _Pending_.

## Accept the peering request in AWS

To complete a _Pending_ AWS peering connection, you need to sign in to AWS, where you accept the peering request. After accepting the request, you will add a route table entry for the application VPC.

{{< tip title="What you need" >}}
The CIDR address of the YugabyteDB Managed VPC you are peering with.

**Where to find it**<br>The **VPC Details** sheet on the [VPCs page](../cloud-add-vpc/) or the **Peering Details** sheet on the [Peering Connections page](../cloud-add-peering/).
{{< /tip >}}

Sign in to your AWS account and navigate to the region hosting the application VPC you want to peer.

### DNS settings

Before accepting the request, ensure that the DNS hostnames and DNS resolution options are enabled for the application VPC. This ensures that the cluster's hostnames in standard connection strings automatically resolve to private instead of public IP addresses when the YugabyteDB Managed cluster is accessed from the application VPC.

To set DNS settings:

1. On the AWS [Your VPCs](https://console.aws.amazon.com/vpc/home?#vpcs) page, select the application VPC in the list.
1. Click **Actions** and choose **Edit DNS hostnames** or **Edit DNS resolution**.
1. Enable the **DNS hostnames** or **DNS resolution** option and click **Save changes**.

### Accept the peering request

To accept the peering request, do the following:

1. On the AWS [Peering Connections](https://console.aws.amazon.com/vpc/home?#PeeringConnections) page, select the pending peering connection in the list; its status is _Pending acceptance_.
1. Click **Actions** and choose **Accept request** to display the **Accept VPC peering connection request** window.

    ![Accept peering in AWS](/images/yb-cloud/cloud-peer-aws-accept.png)

1. Click **Accept request**.

    **Tip**: After accepting the request, click **Modify my route tables now** to navigate directly to [adding a route table entry](#add-the-route-table-entry-in-aws).

On the **Peering connections** page, note the **Peering connection ID**; you will use it when adding the route table entry.

When finished, the status of the peering connection in YugabyteDB Managed changes to _Active_ if the connection is successful.

## Add the route table entry in AWS

Add a route to the route table of the application VPC so that you can send and receive traffic across the peering connection.

Ensure you are signed in to your AWS account and navigate to the region hosting the application VPC being peered.

To add a route table entry:

1. On the AWS [Route Tables](https://console.aws.amazon.com/vpc/home?#RouteTables) page, select the route table associated with the subnet of the application VPC.
1. Click **Actions** and choose **Edit routes** to display the **Edit routes** window.

    ![Add routes in AWS](/images/yb-cloud/cloud-peer-aws-route.png)

1. Click **Add route**.
1. Add the YugabyteDB Managed VPC CIDR address to the **Destination** column, and the Peering connection ID to the **Target** column.
1. Click **Save changes**.

If your application runs in multiple subnets that use separate route tables, repeat these steps for all route tables associated with your application subnets.

## Deploy a cluster in the VPC

You can deploy your cluster in a VPC any time after the VPC is created. You must deploy the cluster in the VPC; the VPC can't be changed after cluster creation.

To deploy a cluster in a VPC:

1. On the **Clusters** page, click **Add Cluster**.
1. Choose **Dedicated**.
1. Enter a name for the cluster, choose **AWS**, and click **Next**.
1. For a **Single-Region Deployment**, choose the region where the VPC is deployed, and under **Configure VPC**, choose **Use VPC peering**, and select your VPC.

    For a **Multi-Region Deployment**, select each region and its corresponding VPC.

For more information on creating clusters, refer to [Create a cluster](../../create-clusters/).

## Add the application VPC to the cluster IP allow list

To enable the peered application VPC to connect to the cluster, you need to add the peered VPC to the cluster IP allow list.

To add the application VPC to the cluster IP allow list:

1. On the **Clusters** page, select the cluster you are peering, click **Actions**, and choose **Edit IP Allow List** to display the **Add IP Allow List** sheet.

1. Click **Add Peered VPC Networks**.

1. Click **Save** when done.

For more information on IP allow lists, refer to [IP allow lists](../../../cloud-secure-clusters/add-connections/).

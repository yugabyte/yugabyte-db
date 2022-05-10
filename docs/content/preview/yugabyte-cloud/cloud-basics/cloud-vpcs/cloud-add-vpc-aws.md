---
title: Create a VPC Network
headerTitle:
linkTitle: Create a VPC Network
description: Create and manage your cloud VPCs.
menu:
  preview:
    identifier: cloud-add-vpc-1-aws
    parent: cloud-vpcs
    weight: 40
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../cloud-add-vpc-aws/" class="nav-link active">
      <i class="fab fa-aws" aria-hidden="true"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../cloud-add-vpc-gcp/" class="nav-link">
       <i class="fab fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

</ul>

YugabyteDB Managed supports virtual private cloud (VPC) networks on AWS and GCP.

Using YugabyteDB Managed, you can create a VPC on AWS, deploy clusters in the VPC, and peer the VPC with application VPCs hosted on AWS.

To create a VPC network in AWS, you need to complete the following tasks:

| Task | Notes |
| :--- | :--- |
| 1. Create the VPC. | Reserves a range of IP addresses for the network.<br>The status of the VPC is _Active_ when done. |
| 2. Deploy a cluster in the VPC. | This can be done at any time - you don't need to wait until the VPC is peered. |
| 3. Create a peering connection. | Connects your VPC and the application VPC on the cloud provider network.<br>The status of the peering connection is _Pending_ when done. |
| 4. Accept the peering request in AWS. | Confirms the connection between your VPC and the application VPC.<br>The status of the peering connection is _Active_ when done. |
| 5. Add the application VPC to the IP allow list. | Allows the peered application VPC to connect to the cluster.<br>Add at least one of the CIDR blocks associated with the peered application VPC to the [IP allow list](../../../cloud-secure-clusters/add-connections/) for your cluster. |

With the exception of 4, these tasks are performed in YugabyteDB Managed.

For information on VPC network peering in AWS, refer to [VPC Peering](https://docs.aws.amazon.com/vpc/latest/userguide/vpc-peering.html) in the AWS documentation.

## Create a VPC

> You need the CIDR range for the application VPC with which you want to peer, as the addresses can't overlap. To view the application VPC CIDR address, navigate to the AWS [Your VPCs](https://console.aws.amazon.com/vpc/home?#vpcs) page for the region hosting the VPC you want to peer.

To create a VPC, do the following:

1. On the **Network Access** page, select **VPC Network**, then **VPCs**.
1. Click **Create VPC** to display the **Create VPC** sheet.
1. Enter a name for the VPC.
1. Choose the provider (AWS).
1. Select the region and specify the CIDR address.
1. Click **Save**.

YugabyteDB Managed adds the VPC to the VPCs list with a status of _Creating_. If successful, after a minute or two, the status will change to _Active_.

The VPC's network name and project ID are automatically assigned. You'll need these details when configuring the peering in GCP.

## Deploy a cluster in the VPC

You can deploy your cluster in a VPC any time after the VPC is created.

To deploy a cluster in a VPC:

1. On the **Clusters** page, click **Add Cluster**.
1. Choose **YugabyteDB Managed** and click **Next**.
1. Choose the provider you used for your VPC.
1. Enter a name for the cluster.
1. Select the **Region**. Choose the region where the VPC is deployed.
1. Set the Fault Tolerance. For production clusters, typically this will be Availability Zone Level.
1. Under **Network Access**, choose **Deploy this cluster in a dedicated VPC**, and select your VPC.
1. Click **Create Cluster**.

For more information on creating clusters, refer to [Create a cluster](../../create-clusters/).

## Create a peering connection

After creating a VPC in YugabyteDB Managed that uses AWS, you can peer it with an AWS application VPC.

> You need the following details for the AWS application VPC you are peering with:
>
> - Account ID
> - VPC ID
> - VPC region
> - VPC CIDR address
>
> To obtain these details, navigate to your AWS [Your VPCs](https://console.aws.amazon.com/vpc/home?#vpcs) page for the region hosting the VPC you want to peer.

To create a peering connection, in YugabyteDB Managed do the following:

1. On the **Network Access** page, select **VPC Network**, then **Peering Connections**.
1. Click **Add Peering Connection** to display the **Create Peering** sheet.
1. Enter a name for the peering connection.
1. Choose **AWS**.
1. Choose the YugabyteDB Managed VPC you are peering. Only VPCs that use AWS are listed.
1. Enter the AWS account ID, and the application VPC ID, region, and CIDR address.
1. Click **Initiate Peering**.

The peering connection is created with a status of _Pending_.

## Accept the peering request in AWS

To complete a _Pending_ AWS peering connection, you need to sign in to AWS, accept the peering request, and add a routing table entry.

> You need the CIDR address of the YugabyteDB Managed VPC you are peering with. You can view and copy this in the **VPC Details** sheet on the [VPCs page](../cloud-add-vpc/) or the **Peering Details** sheet on the [Peering Connections page](../cloud-add-peering/).

Sign in to your AWS account and navigate to the region hosting the VPC you want to peer.

### DNS settings

Before accepting the request, ensure that the DNS hostnames and DNS resolution options are enabled for the application VPC. This ensures that the cluster's hostnames in standard connection strings automatically resolve to private instead of public IP addresses when the YugabyteDB Managed cluster is accessed from the application VPC.

To set DNS settings:

1. On the AWS [Your VPCs](https://console.aws.amazon.com/vpc/home?#vpcs) page, select the VPC in the list.
1. Click **Actions** and choose **Edit DNS hostnames** or **Edit DNS resolution**.
1. Enable the **DNS hostnames** or **DNS resolution** option and click **Save changes**.

### Accept the peering request

To accept the peering request, do the following:

1. On the AWS [Peering Connections](https://console.aws.amazon.com/vpc/home?#PeeringConnections) page, select the VPC in the list; its status is Pending request.
1. Click **Actions** and choose **Accept request** to display the **Accept VPC peering connection request** window.
    ![Accept peering in AWS](/images/yb-cloud/cloud-peer-aws-accept.png)
1. Click **Accept request**.

On the **Peering connections** page, note the **Peering connection ID**; you will use it when adding the routing table entry.

### Add the routing table entry

To add a routing table entry:

1. On the AWS [Route Tables](https://console.aws.amazon.com/vpc/home?#RouteTables) page, select the route table associated with the VPC peer.
1. Click **Actions** and choose **Edit routes** to display the **Edit routes** window.
    ![Add routes in AWS](/images/yb-cloud/cloud-peer-aws-route.png)
1. Click **Add route**.
1. Add the YugabyteDB Managed VPC CIDR address to the **Destination** column, and the Peering connection ID to the **Target** column.
1. Click **Save changes**.

When finished, the status of the peering connection in YugabyteDB Managed changes to _Active_ if the connection is successful.

## Add the application VPC to the cluster IP allow list

> You need the CIDR address for the AWS application VPC you are peering with.

To add the application VPC to the cluster IP allow list:

1. On the **Clusters** page, select the cluster you are peering, and click **Add IP Allow List** to display the **Add IP Allow List** sheet.

1. Click **Create New List and Add to Cluster**.

1. Enter a name and description for the list.

1. Add at least one of the CIDR blocks associated with the peered application VPC.

1. Click **Save** when done.

For more information on IP allow lists, refer to [IP allow lists](../../../cloud-secure-clusters/add-connections/).

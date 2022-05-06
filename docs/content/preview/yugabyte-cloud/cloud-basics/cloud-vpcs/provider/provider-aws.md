To complete a _Pending_ peering connection, you need to sign in to AWS, accept the peering request, and add a routing table entry.

For information on VPC network peering in AWS, refer to [VPC Peering](https://docs.aws.amazon.com/vpc/latest/userguide/vpc-peering.html) in the AWS documentation.

**Prerequisites**

You'll need the CIDR address of the YugabyteDB Managed VPC you are peering with. You can view and copy this in the **VPC Details** sheet on the **VPCs** page or the **Peering Details** sheet on the **Peering Connections** page.

After you sign in to your AWS account, navigate to the region hosting the VPC you want to peer.

**DNS settings**

Before accepting the request, ensure that the DNS hostnames and DNS resolution options are enabled for the application VPC. This ensures that the cluster's hostnames in standard connection strings automatically resolve to private instead of public IP addresses when the YugabyteDB Managed cluster is accessed from the application VPC. To set DNS settings:

1. On the AWS [Your VPCs](https://console.aws.amazon.com/vpc/home?#vpcs) page, select the VPC in the list.
1. Click **Actions** and choose **Edit DNS hostnames** or **Edit DNS resolution**.
1. Enable the **DNS hostnames** or **DNS resolution** option and click **Save changes**.

**Accept the peering request**

To accept the peering request, do the following:

1. On the AWS [Peering Connections](https://console.aws.amazon.com/vpc/home?#PeeringConnections) page, select the VPC in the list; its status is Pending request.
1. Click **Actions** and choose **Accept request** to display the **Accept VPC peering connection request** window.
    ![Accept peering in AWS](/images/yb-cloud/cloud-peer-aws-accept.png)
1. Click **Accept request**.

On the **Peering connections** page, note the **Peering connection ID**; you will use it when adding the routing table entry.

**Add the routing table entry**

To add a routing table entry:

1. On the AWS [Route Tables](https://console.aws.amazon.com/vpc/home?#RouteTables) page, select the route table associated with the VPC peer.
1. Click **Actions** and choose **Edit routes** to display the **Edit routes** window.
    ![Add routes in AWS](/images/yb-cloud/cloud-peer-aws-route.png)
1. Click **Add route**.
1. Add the YugabyteDB Managed VPC CIDR address to the **Destination** column, and the Peering connection ID to the **Target** column.
1. Click **Save changes**.

When finished, the status of the peering connection in YugabyteDB Managed changes to _Active_ if the connection is successful.

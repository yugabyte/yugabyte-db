Before you can peer with an AWS application VPC, you must have created at least one VPC in Yugabyte Cloud that uses AWS.

You need the following details for the AWS application VPC you are peering with:

- Account ID
- VPC ID
- VPC region
- VPC CIDR address

To obtain these details, navigate to your AWS [Your VPCs](https://console.aws.amazon.com/vpc/home?#vpcs) page.

To create a peering connection, in Yugabyte Cloud do the following:

1. On the **Network Access** page, select **VPC Network**, then **Peering Connections**.
1. Click **Create Peering** to display the **Create Peering** sheet.
1. Enter a name for the peering connection.
1. Choose **AWS**.
1. Choose the Yugabyte Cloud VPC you are peering. Only VPCs that use AWS are listed.
1. Enter the AWS account ID, and the application VPC ID, region, and CIDR address.
1. Click **Initiate Peering**.

The peering connection is created with a status of _Pending_. To complete the peering, you must accept the peering request in your AWS account.

### Accept the peering request in AWS

To complete a _Pending_ peering connection, you need to sign in to AWS, accept the peering request, and add a routing table entry.

Before accepting the request, ensure that the DNS hostnames and DNS resolution options are enabled for the VPC. This ensures that the cluster's hostnames in standard connection strings automatically resolve to private instead of public IP addresses when the Yugabyte Cloud cluster is accessed from the application VPC.

1. On the AWS [Your VPCs](https://console.aws.amazon.com/vpc/home?#vpcs) page, select the VPC in the list.
1. Click **Actions** and choose **Edit DNS hostnames** or **Edit DNS resolution**.
1. Enable the **DNS hostnames** or **DNS resolution** option and click **Save changes**.

To accept the peering request, do the following:

1. On the AWS [Peering Connections](https://console.aws.amazon.com/vpc/home?#PeeringConnections) page, select the VPC in the list.
1. Click **Actions** and choose **Accept request**.

To add a routing table entry:

1. On the AWS [Route Tables](https://console.aws.amazon.com/vpc/home?#RouteTables) page, select the route table associated with the VPC peer.
1. Click **Actions** and choose **Edit routes**.
    ![Add routes in AWS](/images/yb-cloud/cloud-peer-aws-route.png)
1. Click **Add route**.
1. Add the Yugabyte Cloud cluster CIDR address to the **Destination** column, and the Peering Connection ID to the **Target** column.
1. Click **Save changes**.

When finished, the status of the peering connection in Yugabyte Cloud changes to _Active_ if the connection is successful.

For information on VPC network peering in AWS, refer to [VPC Peering](https://docs.aws.amazon.com/vpc/latest/userguide/vpc-peering.html) in the AWS documentation.

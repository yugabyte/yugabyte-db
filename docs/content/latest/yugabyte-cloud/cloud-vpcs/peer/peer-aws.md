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

To complete a _Pending_ peering connection, you need to sign in to AWS and accept the peering request.

Use the VPC Dashboard to accept the peering request, enable DNS, and add a route table entry.

To make an AWS peering connection active, in AWS, use the **VPC Dashboard** to do the following:

1. Enable DNS hostnames and DNS resolution. This ensures that the cluster's hostnames in standard connection strings automatically resolve to private instead of public IP addresses when the Yugabyte Cloud cluster is accessed from the application VPC.
1. Accept the [peering connection](https://console.aws.amazon.com/vpc/home?#PeeringConnections) request that you received from Yugabyte.
1. Add a [route table](https://console.aws.amazon.com/vpc/home?#RouteTables) entry to the VPC peer and add the Yugabyte Cloud cluster CIDR block to the **Destination** column, and the Peering Connection ID to the **Target** column.

When finished, the status of the peering connection in Yugabyte Cloud changes to _Active_ if the connection is successful.

For information on VPC network peering in AWS, refer to [VPC Peering](https://docs.aws.amazon.com/vpc/latest/userguide/vpc-peering.html) in the AWS documentation.

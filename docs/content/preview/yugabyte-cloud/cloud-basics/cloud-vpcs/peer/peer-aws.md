**Prerequisites**

Before you can peer with an AWS application VPC, you must have created at least one VPC in YugabyteDB Managed that uses AWS.

You need the following details for the AWS application VPC you are peering with:

- Account ID
- VPC ID
- VPC region
- VPC CIDR address

To obtain these details, navigate to your AWS [Your VPCs](https://console.aws.amazon.com/vpc/home?#vpcs) page for the region hosting the VPC you want to peer.

**Add a connection**

To create a peering connection, in YugabyteDB Managed do the following:

1. On the **Network Access** page, select **VPC Network**, then **Peering Connections**.
1. Click **Add Peering Connection** to display the **Create Peering** sheet.
1. Enter a name for the peering connection.
1. Choose **AWS**.
1. Choose the YugabyteDB Managed VPC you are peering. Only VPCs that use AWS are listed.
1. Enter the AWS account ID, and the application VPC ID, region, and CIDR address.
1. Click **Initiate Peering**.

The peering connection is created with a status of _Pending_. To complete the peering, sign in to AWS and accept the peering request. Refer to [Configure provider](../cloud-configure-provider/).

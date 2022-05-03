**Prerequisites**

Before you can peer with a GCP application VPC, you must have created at least one VPC in YugabyteDB Managed that uses GCP.

You need the following details for the GCP application VPC you are peering with:

- GCP project ID
- VPC name
- VPC CIDR address

To obtain these details, navigate to your GCP [VPC networks](https://console.cloud.google.com/networking/networks) page.

**Add a connection**

To create a peering connection, do the following:

1. On the **Network Access** page, select **VPC Network**, then **Peering Connections**.
1. Click **Add Peering Connection** to display the **Create Peering** sheet.
1. Enter a name for the peering connection.
1. Choose **GCP**.
1. Choose the YugabyteDB Managed VPC. Only VPCs that use GCP are listed.
1. Enter the GCP Project ID, application VPC network name, and, optionally, VPC CIDR address.
1. Click **Initiate Peering**.

The peering connection is created with a status of _Pending_. To complete the peering, sign in to GCP and create a peering connection. Refer to [Configure provider](../cloud-configure-provider/).

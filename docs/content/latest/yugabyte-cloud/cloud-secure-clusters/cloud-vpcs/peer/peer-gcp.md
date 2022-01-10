Before you can peer with a GCP application VPC, you must have created at least one VPC in Yugabyte Cloud that uses GCP.

You need the following details for the GCP application VPC you are peering with:

- GCP project ID
- VPC name
- VPC CIDR address

To obtain these details, navigate to your GCP [VPC networks](https://console.cloud.google.com/networking/networks) page.

To create a peering connection, do the following:

1. On the **Network Access** page, select **VPC Network**, then **Peering Connections**.
1. Click **Add Peering Connection** to display the **Create Peering** sheet.
1. Enter a name for the peering connection.
1. Choose **GCP**.
1. Choose the Yugabyte Cloud VPC. Only VPCs that use GCP are listed.
1. Enter the GCP Project ID, application VPC network name, and, optionally, VPC CIDR address.
1. Click **Initiate Peering**.

The peering connection is created with a status of _Pending_. To complete the peering, you must create a peering connection in GCP.

### Create a peering connection in GCP

To complete a _Pending_ peering connection, you need to sign in to GCP and create a peering connection.

You'll need the the **Project ID** and **VPC network name** of the Yugabyte Cloud VPC you are peering with. You can view and copy these details in the **VPC Details** sheet on the **VPCs** page or the **Peering Details** sheet on the **Peering Connections** page.

In the Google Cloud Console, do the following:

1. Under **VPC network**, select [VPC network peering](https://console.cloud.google.com/networking/peering) and click **Create Peering Connection**.\
\
    ![VPC network peering in GCP](/images/yb-cloud/cloud-peer-gcp-1.png)

1. Click **Continue** to display the **Create peering connection** details.\
\
    ![Create peering connection in GCP](/images/yb-cloud/cloud-peer-gcp-2.png)

1. Enter a name for the GCP peering connection.
1. Select your VPC network name.
1. Select **In another project** and enter the **Project ID** and **VPC network name** of the Yugabyte Cloud VPC you are peering with.
1. Click **Create**.

When finished, the status of the peering connection in Yugabyte Cloud changes to _Active_ if the connection is successful.

For information on VPC network peering in GCP, refer to [VPC Network Peering overview](https://cloud.google.com/vpc/docs/vpc-peering) in the Google VPC documentation.

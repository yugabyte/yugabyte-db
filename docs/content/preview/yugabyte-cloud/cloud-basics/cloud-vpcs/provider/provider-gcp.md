To complete a _Pending_ peering connection, you need to sign in to GCP and create a peering connection.

For information on VPC network peering in GCP, refer to [VPC Network Peering overview](https://cloud.google.com/vpc/docs/vpc-peering) in the Google VPC documentation.

**Prerequisites**

You'll need the **Project ID** and **VPC network name** of the YugabyteDB Managed VPC you are peering with. You can view and copy these details in the **VPC Details** sheet on the **VPCs** page or the **Peering Details** sheet on the **Peering Connections** page.

**Create the connection**

In the Google Cloud Console, do the following:

1. Under **VPC network**, select [VPC network peering](https://console.cloud.google.com/networking/peering) and click **Create Peering Connection**.\
\
    ![VPC network peering in GCP](/images/yb-cloud/cloud-peer-gcp-1.png)

1. Click **Continue** to display the **Create peering connection** details.\
\
    ![Create peering connection in GCP](/images/yb-cloud/cloud-peer-gcp-2.png)

1. Enter a name for the GCP peering connection.
1. Select your VPC network name.
1. Select **In another project** and enter the **Project ID** and **VPC network name** of the YugabyteDB Managed VPC you are peering with.
1. Click **Create**.

When finished, the status of the peering connection in YugabyteDB Managed changes to _Active_ if the connection is successful.

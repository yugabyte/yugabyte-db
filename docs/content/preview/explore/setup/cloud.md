Examples requiring a single-node cluster can be run using the free Sandbox cluster.

If you haven't already created your sandbox cluster, log in to YugabyteDB Managed, on the **Clusters** page click **Add Cluster**, and follow the instructions in the **Create Cluster** wizard.

Save your cluster credentials in a convenient location, you will use them to connect to your cluster.

Before you can create a multi-node cluster in YugabyteDB Managed, you need to [add your billing profile and payment method](../yugabyte-cloud/cloud-admin/cloud-billing-profile/), or you can [request a free trial](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431).

To create a single region multi-node cluster, refer to [Create a single-region cluster](../yugabyte-cloud/cloud-basics/create-clusters/create-single-region/).

### Connect to your clusters

You can run Explore exercises in YugabyteDB Managed using the Cloud Shell:

1. In YugabyteDB Managed, on the **Clusters** page, select your cluster.
1. Click **Connect**.
1. Click **Launch Cloud Shell**.
1. Enter the user name from the cluster credentials you downloaded when you created the cluster.
1. Select the API to use (YSQL or YCQL) and click **Confirm**.
  The shell displays in a separate browser page. Cloud Shell can take up to 30 seconds to be ready.
1. Enter the password from the cluster credentials you downloaded when you created the cluster.

Note that if your Cloud Shell session is idle for more than 5 minutes, your browser may disconnect you. To resume, close the browser tab and connect again.

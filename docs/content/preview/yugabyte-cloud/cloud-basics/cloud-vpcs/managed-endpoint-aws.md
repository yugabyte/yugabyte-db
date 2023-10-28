---
title: Connect VPCs using AWS PrivateLink
headerTitle: Set up private link
linkTitle: Set up private link
description: Connect to a VPC in AWS using PrivateLink.
headcontent: Connect your endpoints using PrivateLink
menu:
  preview_yugabyte-cloud:
    identifier: managed-endpoint-1-aws
    parent: cloud-add-endpoint
    weight: 50
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../managed-endpoint-aws/" class="nav-link active">
      <i class="fa-brands fa-aws" aria-hidden="true"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../managed-endpoint-azure/" class="nav-link">
       <i class="fa-brands fa-microsoft" aria-hidden="true"></i>
      Azure
    </a>
  </li>

</ul>

Connect your cluster to an application VPC via AWS PrivateLink.

{{< youtube id="IQ1mfp5XnOQ" title="Set up a private link for clusters deployed in AWS" >}}

To use AWS PrivateLink, you need the following:

- An AWS user account with an IAM user policy that grants permissions to create, modify, describe, and delete endpoints.
- The Amazon resource names (ARN) of [security principals](https://docs.aws.amazon.com/vpc/latest/privatelink/configure-endpoint-service.html#add-remove-permissions) to which to grant access to the endpoint.

Make sure that default security group in your application VPC allows internal connectivity. Otherwise, your application may not be able to reach the endpoint.

Make sure that the **Enable DNS resolution** and **Enable DNS hostnames** DNS settings in your application VPC are enabled. To access these settings, in the AWS [VPC console](https://console.aws.amazon.com/vpc/), select the VPC, click **Actions**, and choose **Edit VPC Settings**.

## Set up a private link for AWS

To use PrivateLink to connect your cluster to a VPC in AWS that hosts your application, first create a private service endpoint (PSE) on your cluster, then create an endpoint in AWS.

### Create a PSE in YugabyteDB Managed

To create a PSE, do the following:

1. Enter the following command:

    ```sh
    ybm cluster network endpoint create \
      --cluster-name <yugabytedb_cluster> \
      --region <cluster_region> \
      --accessibility-type PRIVATE_SERVICE_ENDPOINT \
      --security-principals <amazon_resource_names>
    ```

    Replace values as follows:

    - `yugabytedb_cluster` - name of your cluster.
    - `cluster_region` - cluster region where you want to place the PSE. Must match one of the regions where your cluster is deployed (for example, `us-west-2`), as well as the region where your application is deployed.
    - `amazon_resource_names` - comma-separated list of the ARNs of security principals that you want to grant access. For example, `arn:aws:iam::<aws account number>:root`.

1. Note the endpoint ID in the response.

    You can also display the endpoint ID by entering the following command:

    ```sh
    ybm cluster network endpoint list --cluster-name <yugabytedb_cluster>
    ```

    This outputs the IDs of all the cluster endpoints.

1. After the endpoint becomes ACTIVE, display the service name of the PSE by entering the following command:

    ```sh
    ybm cluster network endpoint describe --cluster-name <yugabytedb_cluster> --endpoint-id <endpoint_id>
    ```

    Note the service name of the endpoint you want to link to your client application VPC in AWS.

### Create a private endpoint in AWS

You can create the AWS endpoint using the AWS [VPC console](https://console.aws.amazon.com/vpc/) or from the command line using the [AWS CLI](https://docs.aws.amazon.com/cli/).

#### Use the Amazon VPC console

To create an interface endpoint to connect to your cluster PSE, do the following:

1. Open the Amazon [VPC console](https://console.aws.amazon.com/vpc/).

1. Switch to the region where your YugabyteDB cluster is deployed.

1. In the navigation pane, choose **Endpoints** and click **Create endpoint**.

1. Enter a Name tag for the endpoint.

1. Under **Service Category**, select **Other endpoint services**.

1. Under **Service settings**, in the **Service name** field, enter the service name of your cluster PSE and click **Verify service**.

    ![AWS endpoint service](/images/yb-cloud/managed-endpoint-aws-1.png)

1. In the **VPC** field, enter the ID of the VPC where you want to create the AWS endpoint.

1. Under **Subnets**, select the subnets (Availability Zones) to use. At least one of the subnets should match the zones in your cluster.

1. Under **Security groups**, select the security groups to associate with the endpoint network interfaces.

    ![AWS endpoint service](/images/yb-cloud/managed-endpoint-aws-2.png)

1. Click **Create endpoint**.

    The endpoint is added to the Endpoints in AWS.

1. Select the endpoint and on the endpoint details page, click **Actions** and choose **Modify private DNS name** so that the **Private DNS names enabled** setting is set to Yes.

    ![AWS endpoint service](/images/yb-cloud/managed-endpoint-aws-dns.png)

The initial endpoint status is _Pending_. After the link is validated, the status is _Available_. The private DNS name may take a few minutes to propagate before you can connect.

#### Use AWS CLI

Enter the following command:

```sh
aws ec2 create-vpc-endpoint --vpc-id <application_vpc_id> \
  --region <region> --service-name <pse_service_name> \
  --vpc-endpoint-type Interface --subnet-ids <subnet_ids> \
  --private-dns-enabled
```

Replace values as follows:

- `application_vpc_id` - ID of the AWS VPC. Find this value on the VPC dashboard in your AWS account.
- `region` - region where you want the VPC endpoint. The region needs to be the same as a region where your cluster is deployed.
- `pse_service_name` - service name of your PSE.
- `subnet_ids` - string that identifies the [subnets](https://docs.aws.amazon.com/vpc/latest/userguide/modify-subnets.html) that your AWS VPC uses. Find these values under **Subnets** in your AWS VPC console.

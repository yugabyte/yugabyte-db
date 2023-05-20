---
title: Private service endpoints
headerTitle:
linkTitle: Private service endpoints
description: Manage private service endpoints to your VPCs.
headcontent: Manage private service endpoints for your VPCs
menu:
  preview_yugabyte-cloud:
    identifier: cloud-add-endpoint
    parent: cloud-vpcs
    weight: 35
type: docs
---

A private service endpoint (PSE) can be used to connect a YugabyteDB Managed cluster that is deployed in a VPC with other services on the same cloud provider - typically one that hosts an application that you want to have access to your cluster. Unlike VPC peering, when connected to a VPC using a private link, you do not need to add an IP allow list to your cluster.

You must create a VPC and deploy your cluster before you can configure a PSE.

## Limitations

- Currently, PSEs are only supported for [AWS PrivateLink](https://docs.aws.amazon.com/vpc/latest/privatelink/what-is-privatelink.html).
- Currently, PSEs must be created and managed using [ybm CLI](../../../managed-automation/managed-cli/).

## Prerequisites

Before you can create a PSE, you need to do the following:

- Create a VPC. Refer to [Create a VPC](../cloud-add-vpc/#create-a-vpc). Make sure your VPC is in the same region as the application VPC to which you will connect your endpoint.
- Deploy a YugabyteDB cluster in the VPC. Refer to [Create a cluster](../../create-clusters/).

To use ybm CLI, you need to do the following:

- Create an API key. Refer to [API keys](../../../managed-automation/managed-apikeys/).
- Install and configure ybm CLI. Refer to [Install and configure](../../../managed-automation/managed-cli/managed-cli-overview/).

In addition, to use AWS PrivateLink, you need the following:

- An AWS user account with an IAM user policy that grants permissions to create, modify, describe, and delete endpoints.
- The Amazon resource names (ARN) of [security principals](https://docs.aws.amazon.com/vpc/latest/privatelink/configure-endpoint-service.html#add-remove-permissions) to which to grant access to the endpoint.

Make sure that default security group in your application VPC allows internal connectivity. Otherwise, your application may not be able to reach the endpoint.

Make sure that the **Enable DNS resolution** and **Enable DNS hostnames** DNS settings in your application VPC are enabled. To access these settings, in the AWS [VPC console](https://console.aws.amazon.com/vpc/), select the VPC, click **Actions**, and choose **Edit VPC Settings**.

## Create a PSE for AWS PrivateLink using ybm CLI

To use PrivateLink to connect your cluster to a VPC that hosts your application, first create a PSE on your cluster, then create an endpoint in AWS.

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

### Create the AWS VPC endpoint in AWS

You can create the AWS endpoint using the AWS [VPC console](https://console.aws.amazon.com/vpc/) or from the command line using the [AWS CLI](https://docs.aws.amazon.com/cli/).

#### Use the Amazon VPC console

To create an interface endpoint to connect to your cluster endpoint, do the following:

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

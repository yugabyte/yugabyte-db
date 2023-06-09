---
title: Connect VPCs using Azure Private Link
headerTitle: Connect VPCs
linkTitle: Connect VPCs
description: Connect to a VPC in Azure using Private Link.
headcontent: Connect your cluster VPC with a VPC in Azure using Private Link
menu:
  preview_yugabyte-cloud:
    identifier: managed-endpoint-1-azure
    parent: cloud-add-endpoint
    weight: 50
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../managed-endpoint-aws/" class="nav-link">
      <i class="fa-brands fa-aws" aria-hidden="true"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../managed-endpoint-azure/" class="nav-link active">
       <i class="fa-brands fa-microsoft" aria-hidden="true"></i>
      Azure
    </a>
  </li>

</ul>

To use Azure Private Link, you need the following:

- An Azure user account with an IAM user policy that grants permissions to create, modify, describe, and delete endpoints.
- The Amazon resource names (ARN) of [security principals](https://docs.aws.amazon.com/vpc/latest/privatelink/configure-endpoint-service.html#add-remove-permissions) to which to grant access to the endpoint.

Make sure that default security group in your application VPC allows internal connectivity. Otherwise, your application may not be able to reach the endpoint.

## Create a PSE for Azure Private Link using ybm CLI

To use Private Link to connect your cluster to a VPC that hosts your application, first create a PSE on your cluster, then create an endpoint in Azure.

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

    Note the service name of the endpoint you want to link to your client application VPC in Azure.

### Create the Azure VPC endpoint in Azure

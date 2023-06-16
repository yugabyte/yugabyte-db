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

- An Azure user account with an active subscription.
- An Azure service that supports private endpoints.
- The [subscription ID](https://learn.microsoft.com/en-us/azure/azure-portal/get-subscription-tenant-id) of the service to which to grant access to the endpoint.

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
      --subscription-id <subscription_ids>
    ```

    Replace values as follows:

    - `yugabytedb_cluster` - name of your cluster.
    - `cluster_region` - cluster region where you want to place the PSE. Must match one of the regions where your cluster is deployed (for example, `us-west-2`), as well as the region where your application is deployed.
    - `subscription_id` - comma-separated list of the subscription IDs of services that you want to grant access.

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

### Create the private endpoint in Azure

You can create the Azure endpoint using the [Azure CLI](https://learn.microsoft.com/en-us/cli/azure/).

To create the private endpoint and connect it to your YBM PSE (called a private link service in Azure), enter the following command:

```sh
az network private-endpoint create \
--connection-name <private_link_service_connection_name> \
--name <private_endpoint_name> \
--private-connection-resource-id <pse_resource_id> \
--resource-group <resource_group_name> \
--subnet <subnet_id> \
--vnet-name <private_endpoint_vnet_name> \
–-location <PRIVATE_ENDPOINT_REGION_NAME>
```

Replace values as follows:

- `private_link_service_connection_name` - provide a name for the private link connection from the private endpoint to the private link service.
- `private_endpoint_name` - provide a name for the private endpoint.
- `pse_resource_id` - a property of your YBM PSE; for example, "/subscriptions/00000000-0000-0000-0000-000000000000/resourceGroups/MyResourceGroup/providers/Microsoft.Network/privateLinkServices/MyPLS".
- `resource_group_name` - the resource group in which the private endpoint will be created.
- `subnet_id` - the ID of the subnet in the resource group in which the private endpoint will be created.
- `private_endpoint_vnet_name` - the name of the Azure Virtual Network (VNet) where the private endpoint will be created.
- `private_endpoint_region_name` - the Azure region in which the private endpoint and VNet are present.

To ensure no change to your application settings and connect using sslmode=verify-full, create a private DNS zone in the same resource group and VNet as the private endpoint, and add an A record pointing to the IP address of the private endpoint.

1. To create a private DNS zone, enter the following command:

    ```sh
    az network private-dns zone create \
        --name <cluster_id>.azure.ybdb.io \
        --resource-group <resource_group_name>
    ```

    Replace values as follows:

    - `cluster_id` - the cluster ID of the cluster with the PSE; the cluster ID is displayed on the cluster **Settings** tab in YBM.
    - `resource_group_name` - the resource group in which the private endpoint was created.

1. To obtain the ipv4 address and VNet of the private endpoint, enter the following command:

    ```sh
    az network private-endpoint show \
    --name <private_endpoint_name> \
    --resource-group <resource_group_name>
    ```

    This command returns the following values that you use to link the private DNS zone to the VNET:

    - ipv4 address of the private endpoint (`private_endpoint_ipv4_address` in the following commands)
    - VNet of the private endpoint (`private_endpoint_vnet` in the following commands)

1. To link the private DNS zone to the VNet containing the private endpoint, enter the following command:

    ```sh
    az network private-dns link vnet create 
        --name <private_dns_zone_name>
        --registration-enabled true
        --resource-group <resource_group_name>
        --virtual-network <private_endpoint_vnet>
        --zone-name <cluster_id>.azure.ybdb.io
        --tags yugabyte
    ```

    Replace values as follows:

    - `private_dns_zone_name` - provide a name for the private DNS zone.
    - `resource_group_name` - the resource group in which the private endpoint was created.
    - `private_endpoint_vnet` - the VNet of the private endpoint.
    - `cluster_id` - the cluster ID of the cluster with the PSE; the cluster ID is displayed on the cluster **Settings** tab in YBM.

1. To create an A record in the private DNS zone, enter the following command:

    ```sh
    az network private-dns record-set a add-record \
    --ipv4-address <private_endpoint_ipv4_address> \
    --record-set-name <record_set_name> \
    --resource-group <resource_group_name> \
    --zone-name <private_dns_zone_name>
    ```

    Replace values as follows:

    - `private_endpoint_ipv4_address` - the IP address of the private endpoint.
    - `record_set_name` - provide a name for the record.
    - `resource_group_name` - the resource group in which the private endpoint was created.
    - `private_dns_zone_name` - the name of the private DNS zone.

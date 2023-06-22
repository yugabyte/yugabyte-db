---
title: Connect VPCs using Azure Private Link
headerTitle: Set up private link
linkTitle: Set up private link
description: Connect to a VNet in Azure using Private Link.
headcontent: Connect your endpoints using Private Link
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
- The [subscription ID](https://learn.microsoft.com/en-us/azure/azure-portal/get-subscription-tenant-id#find-your-azure-subscription) of the service to which to grant access to the cluster endpoint.

Make sure that default security group in your application Azure Virtual Network (VNet) allows internal connectivity. Otherwise, your application may not be able to reach the endpoint.

## Create a PSE for Azure Private Link using ybm CLI

To use Private Link to connect your cluster to an VNet that hosts your application, first create a PSE on your cluster, then create an endpoint in Azure.

### Create a PSE in YugabyteDB Managed

To create a PSE, do the following:

1. Enter the following command:

    ```sh
    ybm cluster network endpoint create \
      --cluster-name <yugabytedb_cluster> \
      --region <cluster_region> \
      --accessibility-type PRIVATE_SERVICE_ENDPOINT \
      --security-principals <subscription_ids>
    ```

    Replace values as follows:

    - `yugabytedb_cluster` - name of your cluster.
    - `cluster_region` - cluster region where you want to place the PSE. Must match one of the regions where your cluster is deployed (for example, `us-west-2`), and preferably match the region where your application is deployed.
    - `subscription_ids` - comma-separated list of the subscription IDs of services that you want to grant access.

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

    Note the service name of the endpoint you want to link to your client application VNet in Azure.

### Create the private endpoint in Azure

You can create the private endpoint using the [Azure Portal](https://portal.azure.com/) or from the command line using the [Azure CLI](https://learn.microsoft.com/en-us/cli/azure/).

#### Use the Azure Portal

To create a private endpoint to connect to your cluster PSE, do the following:

1. Open the [Azure Portal](https://portal.azure.com/).

#### Use Azure CLI

You can create the Azure endpoint using the [Azure CLI](https://learn.microsoft.com/en-us/cli/azure/).

To create the private endpoint and connect it to your YBM PSE (called a private link service in Azure), enter the following command:

```sh
az network private-endpoint create \
    --connection-name <private_link_service_connection_name> \
    --name <private_endpoint_name> \
    --private-connection-resource-id <pse_resource_id> \
    --resource-group <resource_group_name> \
    --subnet <subnet_name> \
    --vnet-name <private_endpoint_vnet_name> \
    –-location <private_endpoint_region_name>
```

Replace values as follows:

- `private_link_service_connection_name` - provide a name for the private link connection from the private endpoint to the private link service.
- `private_endpoint_name` - provide a name for the private endpoint.
- `pse_resource_id` - a property of your YBM PSE; you can find this on the cluster **Settings** tab under **Network Access**.
- `resource_group_name` - the resource group in which the private endpoint will be created.
- `subnet_name` - the name of the subnet in the resource group in which the private endpoint will be created.
- `private_endpoint_vnet_name` - the name of the VNet where the private endpoint will be created.
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

1. To link the private DNS zone to the VNet containing the private endpoint, enter the following command:

    ```sh
    az network private-dns link vnet create 
        --name <private_dns_zone_name>
        --registration-enabled true
        --resource-group <resource_group_name>
        --virtual-network <private_endpoint_vnet_name>
        --zone-name <cluster_id>.azure.ybdb.io
        --tags yugabyte
    ```

    Replace values as follows:

    - `private_dns_zone_name` - provide a name for the private DNS zone.
    - `resource_group_name` - the resource group in which the private endpoint was created.
    - `private_endpoint_vnet_name` - the name of VNet in which the private endpoint was created.
    - `cluster_id` - the cluster ID of the cluster with the PSE; the cluster ID is displayed on the cluster **Settings** tab in YBM.

1. To obtain the Network Interface (NIC) resource ID for the private endpoint, enter the following command:

    ```sh
    az network private-endpoint show \
        --name <private_endpoint_name> \
        --resource-group <resource_group_name>
    ```

    Replace values as follows:

    - `private_endpoint_name` - the name of the private endpoint.
    - `resource_group_name` - the resource group in which the private endpoint was created.

    This command returns the NIC resource ID of the private endpoint (`private_endpoint_nic_resource_id` in the following command).

1. To obtain the ipv4 address of the private endpoint, enter the following command:

    ```sh
    az network nic show \
        --ids <private_endpoint_nic_resource_id> \
        --query "ipConfigurations[0].privateIPAddress"
    ```

    This command returns the ipv4 address of the private endpoint (`private_endpoint_ipv4_address` in the following command).

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

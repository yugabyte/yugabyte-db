---
title: Develop Secure Applications with Azure Private Link
headerTitle: Develop secure applications with Azure Private Link
linkTitle: Azure Private Link
description: Using a Node.js app to demonstrate, see how YugabyteDB enhances Azure connectivity with Azure Private Service Endpoints.
image: /images/tutorials/azure/icons/Private-Link-Icon.svg
headcontent: Secure connections between applications in Azure and YugabyteDB
menu:
  preview:
    identifier: tutorials-azure-private-link
    parent: tutorials-azure
    weight: 50
type: docs
---

YugabyteDB provides multiple networking options to ensure security, reliability, and improved latencies. [Virtual Private Clouds (VPC)](../../../yugabyte-cloud/cloud-basics/cloud-vpcs/) can be connected through a [VPC peering connection](../../../yugabyte-cloud/cloud-basics/cloud-vpcs/cloud-add-peering/) to keep network traffic in a cloud provider's network, bypassing the public internet. This allows applications running in Google Cloud or AWS to connect to YugabyteDB as if they were in the same network.

In Azure, you can achieve a similar result using [Private Service Endpoints](../../../yugabyte-cloud/cloud-basics/cloud-vpcs/cloud-add-endpoint/). To illustrate this point, let's securely connect a Node.js application in Azure to a cluster running in YugabyteDB Managed.

In the following sections, you will:

1. Deploy and configure a YugabyteDB cluster in a VNet in Azure.
1. Create and configure a virtual machine in a VNet in Azure.
1. Set up the private link between the application and database.
1. Run a Node.js application in the virtual machine in Azure.

## Prerequisites

- A YugabyteDB Managed account. Sign up for a [free trial](https://cloud.yugabyte.com/signup/).
- An Azure Cloud account with permission to create services.

## Get started with YugabyteDB Managed

[Create a 3-node cluster](../../../yugabyte-cloud/cloud-basics/create-clusters/create-single-region/) on Azure in the _uswest3_ region.

![3-node YugabyteDB deployment in uswest3](/images/tutorials/azure/azure-private-link/yb-deployment.png "3-node YugabyteDB deployment in uswest3")

Remember to save the credentials after creation and [download the CA certificate](../../../develop/build-apps/cloud-add-ip/#download-your-cluster-certificate) once operational, ensuring a secure connection through the Node.js Smart Client.

## Get started with Azure

To test the connection between Azure and YugabyteDB using Azure Private Link, start by creating a virtual machine in the Azure console.

1. **Create a virtual machine running Ubuntu in Azure.** This machine will run the Node.js process that connects to YugabyteDB Managed.

    ![Create an Ubuntu VM in Azure](/images/tutorials/azure/azure-private-link/azure-create-vm.png "Create an Ubuntu VM in Azure")

1. **Configure the networking settings on this VM, placing it in a Virtual Network (VNet)**. If there is no existing VNet in the region selected (in this case, West US 3), a new one will be created by default.

    ![Configure network settings on the VM](/images/tutorials/azure/azure-private-link/azure-networking.png "Configure network settings on the VM")

1. **Enable a basic Network Security Group** to limit the inbound and outbound traffic to the VM.
1. **Enable SSH access** to securely install system and application dependencies, and copy files to the VM.

## Set up Azure Private Link

Follow [these instructions to configure Azure Private Link](../../../yugabyte-cloud/cloud-basics/cloud-vpcs/managed-endpoint-azure/) between your YugabyteDB cluster and application VPC.

Once completed, you'll have a [Private Service Endpoint](../../../yugabyte-cloud/cloud-basics/cloud-vpcs/cloud-add-endpoint/) which can be used to host our database connection.

## Run a Node.js application on Azure

This basic application can be run inside your Azure VNet to verify the connectivity between your application services and the database cluster.

1. Clone the [application on GitHub](https://github.com/YugabyteDB-Samples/yugabytedb-azure-private-link-demo-nodejs).
1. Edit the database connection details in the _.env_ file and copy the YugabyteDB CA certificate to the root directory of the project.
1. SSH into the virtual machine from the terminal.

    ```sh
    ssh -i  /path/to/vm/private/key.pem azureuser@[PUBLIC_IP_ADDRESS]
    ```

1. Prepare the Node.js runtime environment in the VM.

    ```sh
    sudo apt update
    curl https://raw.githubusercontent.com/creationix/nvm/master/install.sh | bash
    source ~/.bashrc
    nvm install 18
    ```

1. In another terminal window, securely copy the application files to the VM.

    ```sh
    scp -r /path/to/YBAzureNetworking/ azureuser@[PUBLIC_IP_ADDRESS]:/home/azureuser
    ```

1. Install the application dependencies on the VM.

    ```sh
    npm install
    ```

1. Run the application to verify the database connection.

    ```sh
    npm run start
    # Establishing connection with YugabyteDB Managed...
    # Connected successfully.
    ```

## Wrap-up

Azure Private Link simplifies establishing a secure connection between Azure-based applications and YugabyteDB.

If you're interested in developing other applications on Azure, check out [Build Applications Using Azure App Service](/preview/tutorials/azure/azure-app-service/).

If you would like to explore the different deployment options of YugabyteDB (including self-managed, co-managed, fully managed, and open source), explore our [database comparison page](https://www.yugabyte.com/compare-products/).

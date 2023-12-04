---
title: Developing Secure Node.js Applications Using Azure Private Link and YugabyteDB
headerTitle:
linkTitle: Azure Private Link
description: Developing Secure Node.js Applications Using Azure Private Link and YugabyteDB
image: /images/tutorials/azure/icons/Private-Link-Icon.svg
headcontent: Developing Secure Node.js Applications Using Azure Private Link and YugabyteDB
aliases:
  - /preview/tutorials/azure/private-link
menu:
  preview:
    identifier: tutorials-azure-private-link
    parent: tutorials-azure
type: indexpage
---

YugabyteDB provides multiple networking options to ensure security, reliability, and improved latencies. [Virtual Private Clouds (VPC)](https://docs.yugabyte.com/preview/yugabyte-cloud/cloud-basics/cloud-vpcs/) can be connected through a [VPC peering connection](https://docs.yugabyte.com/preview/yugabyte-cloud/cloud-basics/cloud-vpcs/cloud-add-peering/) to keep network traffic within a cloud provider’s network, bypassing the public internet. This allows applications running in Google Cloud or AWS to connect to YugabyteDB as if they were in the same network.

In Azure Cloud, you can achieve a similar result by making use of [Private Service Endpoints](https://docs.yugabyte.com/preview/yugabyte-cloud/cloud-basics/cloud-vpcs/cloud-add-endpoint/). To illustrate this point, let’s securely connect a Node.js application in Azure to a cluster running in YugabyteDB Managed.

## Prerequisites

- An account to Yugabyte’s fully-managed deployment option (formerly known as YugabyteDB Managed).
- An Azure Cloud account with permissions to create services

CTA button: Fully-Managed Free Trial

URL [https://cloud.yugabyte.com/signup/](https://cloud.yugabyte.com/signup/)

## Getting started with YugabyteDB Managed

Create a 3-node cluster on Azure in the \_uswest3 \_region.

![alt_text](/images/tutorials/azure/azure-private-link/yb-deployment.png "image_tooltip")

Remember to save the credentials after creation and [download the CA certificate](https://docs.yugabyte.com/preview/develop/build-apps/cloud-add-ip/#download-your-cluster-certificate) once operational, ensuring a secure connection through the Node.js Smart Client.

## Getting Started with Azure

To test the connection between Azure and YugabyteDB using Azure Private Link, start by creating a virtual machine in the Azure Web Console.

1. **Create a virtual machine running Ubuntu in Azure.** This machine will run the Node.js process that connects to YugabyteDB Managed.

![Create an Ubuntu VM in Azure](/images/tutorials/azure/azure-private-link/azure-create-vm.png "Create an Ubuntu VM in Azure")

2. **Configure the networking settings on this VM, placing it in a Virtual Network (VNet)**. If there is no existing VNet in the region selected (in this case, West US 3), a new one will be created by default.

![Configure network settings on the VM](/images/tutorials/azure/azure-private-link/azure-networking.png "Configure network settings on the VM")

3. **Enable a basic Network Security Group** to limit the inbound and outbound traffic to the VM.
4. **Enable SSH access **to securely install system and application dependencies, and copy files to the VM.

## Setting up Azure Private Link

Follow [these instructions to configure Azure Private Link](https://docs.yugabyte.com/preview/yugabyte-cloud/cloud-basics/cloud-vpcs/managed-endpoint-azure/) between your YugabyteDB cluster and application VPC. Once completed, you’ll have a [Private Service Endpoint](https://docs.yugabyte.com/preview/yugabyte-cloud/cloud-basics/cloud-vpcs/cloud-add-endpoint/) which can be used to host our database connection.

## Running A Node.js Application on Azure

This simple application can be run inside your Azure VNet to verify the connectivity between your application services and the database cluster.

1. Clone the application [LINK TO REPOSITORY] on GitHub.
2. Edit the database connection details in the _.env_ file and copy the YugabyteDB CA certificate to the root directory of the project.
3. SSH into the virtual machine from the terminal.
4. Prepare the Node.js runtime environment in the VM.
5. In another terminal window, securely copy the application files to the VM.
6. Install the application dependencies on the VM.
7. Run the application to verify the database connection.

## Conclusion

Azure Private Link simplifies establishing a secure connection between Azure-based applications and YugabyteDB.

If you’re interested in developing other applications on Azure, check out: \

- [LINK TO AZURE APP SERVICE TUTORIAL IN DOCS]

If you would like to explore the different deployment options of YugabyteDB (including self-managed, co-managed, fully managed, and open source) explore our [database comparison page](https://www.yugabyte.com/compare-products/).

SEO Title: Building Secure Node.js Apps with Azure Private Link and YugabyteDB

Meta: Using a Node.js app to demonstrate, see how YugabyteDB enhances Azure connectivity with Azure Private Service Endpoints.

Excerpt: YugabyteDB offers various networking options like VPC peering, enhancing security, reliability, and latency, allowing applications in GCP or AWS to seamlessly connect as if on the same network. Similarly, in Azure Cloud, Private Service Endpoints achieve comparable results. Let’s see how.

Author: Brett

- Tags: Azure, Application Development, Node.js

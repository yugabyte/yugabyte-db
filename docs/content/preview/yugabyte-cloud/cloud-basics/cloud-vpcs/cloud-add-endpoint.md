---
title: Private service endpoints
headerTitle:
linkTitle: Private service endpoints
description: Manage cluster private service endpoints.
headcontent: Connect clusters to applications using a private link service
menu:
  preview_yugabyte-cloud:
    identifier: cloud-add-endpoint
    parent: cloud-vpcs
    weight: 35
type: docs
---

A private service endpoint (PSE) is used to connect a YugabyteDB Managed cluster that is deployed in a Virtual Private Cloud (VPC) with other services on the same cloud provider - typically a VPC hosting the application that you want to access your cluster. The PSE on your cluster connects to an endpoint on the VPC hosting your application over a private connection.

![VPC network using PSE](/images/yb-cloud/managed-pse-diagram.png)

## Overview

While cloud providers refer to the components of a private link service in different ways, these components serve the same purposes.

| YBM | AWS&nbsp;PrivateLink | Azure&nbsp;Private&nbsp;Link | Description |
| :--- | :--- | :--- | :--- |
| VPC | VPC | VNet | Secure virtual network created on a cloud provider. |
| PSE | [Endpoint service](https://docs.aws.amazon.com/vpc/latest/privatelink/concepts.html#concepts-endpoint-services) | [Private Link service](https://learn.microsoft.com/en-us/azure/private-link/private-link-service-overview) | The endpoints on your cluster that you make available to the private link. |
| Application VPC endpoint | [Interface VPC endpoint](https://docs.aws.amazon.com/vpc/latest/privatelink/concepts.html#concepts-vpc-endpoints) | [Private endpoint](https://learn.microsoft.com/en-us/azure/private-link/private-endpoint-overview) | The endpoints on the application VPC corresponding to the PSEs on your cluster.
| Security principal | [AWS principal](https://docs.aws.amazon.com/vpc/latest/privatelink/configure-endpoint-service.html#add-remove-permissions) (ARN) | [Subscription ID](https://learn.microsoft.com/en-us/azure/azure-portal/get-subscription-tenant-id#find-your-azure-subscription) | Cloud provider account with permissions to manage endpoints. |

Setting up a private link to connect your cluster to your application VPC involves the following tasks:

1. Deploy your cluster in a VPC. You must create a VPC and deploy your cluster before you can configure the PSE.
1. Create a PSE in each region of your cluster. The PSE is an endpoint service, and you activate it by granting access to a security principal on your application VPC.

    In the case of AWS, a security principal is an AWS principal, in the form of Amazon resource names (ARNs).

    For Azure, a security principal is a subscription ID of the service you want to have access.

1. On the cloud provider, create an interface VPC endpoint (AWS) or a private endpoint (Azure) on the VPC (VNet) hosting your application. You create an endpoint for each region in your cluster.

## Limitations

- Currently, PSEs are supported for [AWS PrivateLink](https://docs.aws.amazon.com/vpc/latest/privatelink/what-is-privatelink.html) and [Azure Private Link](https://learn.microsoft.com/en-us/azure/private-link/).
- Currently, PSEs must be created and managed using [ybm CLI](../../../managed-automation/managed-cli/).
- You can't use smart driver load balancing features when connecting to clusters over a private link. See [YugabyteDB smart drivers for YSQL](../../../../drivers-orms/smart-drivers/).

## Prerequisites

Before you can create a PSE, you need to do the following:

1. Create a VPC. Refer to [Create a VPC](../cloud-add-vpc/#create-a-vpc). Make sure your VPC is in the same region as the application VPC to which you will connect your endpoint.
1. Deploy a YugabyteDB cluster in the VPC. Refer to [Create a cluster](../../create-clusters/).

To use ybm CLI, you need to do the following:

- Create an API key. Refer to [API keys](../../../managed-automation/managed-apikeys/).
- Install and configure ybm CLI. Refer to [Install and configure](../../../managed-automation/managed-cli/managed-cli-overview/).

Note that, unlike VPC peering, when connected to an application VPC using a private link, you do not need to add an [IP allow list](../../../cloud-secure-clusters/add-connections/) to your cluster.

## Get started

{{< sections/2-boxes >}}
  {{< sections/bottom-image-box
    title="Set up an AWS PrivateLink"
    description="Add PSEs to your cluster and create interface endpoints on your application VPC in AWS."
    buttonText="Setup Guide"
    buttonUrl="../managed-endpoint-aws/"
  >}}

  {{< sections/bottom-image-box
    title="Set up an Azure Private Link"
    description="Add a PSE to your cluster and create a private endpoint on your application VNet in Azure."
    buttonText="Setup Guide"
    buttonUrl="../managed-endpoint-azure/"
  >}}
{{< /sections/2-boxes >}}

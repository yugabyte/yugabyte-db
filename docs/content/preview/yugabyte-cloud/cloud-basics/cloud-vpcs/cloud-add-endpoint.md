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

A private service endpoint (PSE) is used to connect a YugabyteDB Managed cluster that is deployed in a VPC with other services on the same cloud provider - typically a VPC that hosts an application that you want to have access to your cluster. The PSE connects to an endpoint attached to a VPC hosting your application over a privately linked service. Unlike VPC peering, when connected to a VPC using a private link, you do not need to add an IP allow list to your cluster.

Setting up a private link to connect your cluster to your application VPC involves the following tasks:

1. Deploy your cluster in a VPC. You must create a VPC and deploy your cluster before you can configure a PSE.
1. Create a PSE in each region of your cluster. The PSE is an endpoint service, and it is activated by granting access to a service principal on your application VPC.

    In the case of AWS, the security principal is an AWS principal, in the form of Amazon resource names (ARNs).

    For Azure, the security principal is a subscription ID of the service you want to have access.

1. On the cloud provider, create an interface VPC endpoint (AWS) or private endpoint (Azure) on the VPC (VNet) hosting your application. You create an endpoint for each region in your cluster.

![VPC network using PSE](/images/yb-cloud/managed-pse-diagram.png)

For detailed steps describing how to connect your cluster to an application over a private link using endpoints, refer to [Set up private link](../managed-endpoint-aws/).

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

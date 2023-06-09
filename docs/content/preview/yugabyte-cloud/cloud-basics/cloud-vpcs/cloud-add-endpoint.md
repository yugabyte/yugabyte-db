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

- To learn how to connect VPCs over a private link using endpoints, refer to [Connect VPCs](../managed-endpoint-aws/).

## Limitations

- Currently, PSEs are only supported for [AWS PrivateLink](https://docs.aws.amazon.com/vpc/latest/privatelink/what-is-privatelink.html) and [Azure Private Link](https://learn.microsoft.com/en-us/azure/private-link/).
- Currently, PSEs must be created and managed using [ybm CLI](../../../managed-automation/managed-cli/).

## Prerequisites

Before you can create a PSE, you need to do the following:

- Create a VPC. Refer to [Create a VPC](../cloud-add-vpc/#create-a-vpc). Make sure your VPC is in the same region as the application VPC to which you will connect your endpoint.
- Deploy a YugabyteDB cluster in the VPC. Refer to [Create a cluster](../../create-clusters/).

To use ybm CLI, you need to do the following:

- Create an API key. Refer to [API keys](../../../managed-automation/managed-apikeys/).
- Install and configure ybm CLI. Refer to [Install and configure](../../../managed-automation/managed-cli/managed-cli-overview/).

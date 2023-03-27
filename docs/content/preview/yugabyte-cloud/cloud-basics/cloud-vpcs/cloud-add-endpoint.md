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

A private service endpoint can be used to connect a YugabyteDB Managed VPC with other services on the same cloud provider - typically one that hosts an application that you want to have access to your cluster. A VPC must be created before you can configure a private service endpoint.

## Limitations

- Currently, private service endpoints are only supported on AWS.
- Currently, private service endpoints must be created and managed using [ybm CLI](../../../managed-automation/managed-cli/).

## Prerequisites

Before you can create a private service endpoint, you need to do the following:

- Install and configure ybm CLI. Refer to [Install and configure](../../../managed-automation/managed-cli/managed-cli-overview/).
- Create an API key. Refer to [API keys](../../../managed-automation/managed-apikeys/).
- Create a VPC. Refer to [Create a VPC](../cloud-add-vpc/#create-a-vpc).
- Create a YugabyteDB cluster in the VPC. Refer to [Create a cluster](../../create-clusters/).

In addition, you need the following information:

- The Amazon resource names (ARN) of security principals to which to grant access to the endpoint.

## Create a private service endpoint

To create a private service endpoint using ybm, do the following:

1. Enter the following command:

    ```sh
    ybm cluster network endpoint create \
      --cluster-name yugabytedb_cluster \
      --region resource_region \
      --accessibility-type PRIVATE_SERVICE_ENDPOINT \
      --security-principals amazon_resource_names
    ```

    where

    - `yugabytedb_cluster` is the name of your cluster
    - `resource_region` is the region where you want to place the endpoint. Must match one of the regions where your cluster is deployed. For example, `us-west-2`.
    - `amazon_resource_names` is a comma-separated list of the ARNs of security principals that you want to grant access

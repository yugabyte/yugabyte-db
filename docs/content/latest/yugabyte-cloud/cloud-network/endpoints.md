<!--
title: Endpoints
linkTitle: Endpoints
description: Manage Yugabyte Cloud Endpoints.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: endpoints
    parent: cloud-network
    weight: 200
isTocNested: true
showAsideToc: true
-->

The **Private Endpoint** tab displays a list of endpoints configured for your cloud that includes the endpoint name, provider, region, service, and cluster.

![Cloud Network Endpoint page](/images/yb-cloud/cloud-networking-endpoint.png)

## Add endpoints

To add an endpoint:

1. On the **Private Endpoint** tab, click **Add Endpoint** to display the **Add Endpoint** sheet.
1. Enter a name for the endpoint.
1. Choose the provider and region.
1. Click **Start**.

    Yugabyte cloud begins provisioning the endpoint.

1. Enter your VPC ID and subnet IDs.

## Edit endpoints

To edit an endpoint:

1. On the **Private Endpoint** tab, select an endpoint and click the Edit icon to display the **Edit Endpoint** sheet.
1. Update the name and subnet IDs.

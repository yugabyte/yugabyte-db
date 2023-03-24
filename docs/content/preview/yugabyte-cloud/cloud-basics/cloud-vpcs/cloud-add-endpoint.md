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

You configure private service endpoints using [ybm CLI](../../../managed-automation/managed-cli/).


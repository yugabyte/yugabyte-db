---
title: Configuring VPC peering
linkTitle: Configuring VPC peering
description: Manage Yugabyte Cloud VPC peers.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: cloud-vpcs
    parent: vpc-peers
    weight: 5
isTocNested: true
showAsideToc: true
---

Using VPC peering, you deploy cloud resources in a virtual network that you define. A _VPC_ is a logically isolated, virtual network within a cloud provider. A _peering connection_ is a networking connection between two VPCs that enables you to route traffic between them using private IP addresses. Instances in either VPC can communicate with each other as if they were within the same network, so data can be transferred across these resources with more security.

In the case of Yugabyte Cloud, you create a VPC to host your clusters, and peer it with another VPC that hosts client applications. Any cluster deployed using that VPC will then be able to connect with any application in the client VPC.

To use VPC peering in a cluster, your cluster must be deployed in a dedicated VPC that is peered with your application VPC. You must set up dedicated VPCs and peering before deploying your cluster. VPC peering is not supported in Free clusters.

VPC peering is supported for AWS and GCP. For Azure, contact Yugabyte Cloud support.

## Prerequisites

To set up a VPC peer, you need the details of the VPC you want to peer with. This includes:

- The cloud provider
- Region
- Details of the VPC that you want to with peer with, including
  - AWS account or GCP project
  - VPC ID/network name
  - CIDR blocks of the VPC network

## Creating VPCs

To set up a VPC peer in Yugabyte Cloud, you need to specify the following:

- Cloud provider of choice
- Region
- Preferred CIDR to use for your database VPC.

Refer to [Manage VPCs](../cloud-add-vpc/).

## Creating the peering connection

Once you have created at least one VPC in Yugabyte Cloud for the provider of the VPC you want to peer. You need to specify the following:

- The Yugabyte Cloud VPC to peer.
- VPC peering details, including:
  - GCP - the project ID and the network name that you need to peer to.
  - AWS - the AWS account ID, and the VPC ID, region, and CIDR block.

Refer to [Manage peering connections](../cloud-add-peering/).

### GCP

In the Google Cloud Console, create a peering connection using the project ID and VPC network name.

### AWS

Use the VPC Dashboard to do the following:

- Enable DNS hostnames and DNS resolution. This ensures that the cluster's hostnames in standard connection strings automatically resolve to private instead of public IP addresses when the Yugabyte Cloud cluster is accessed from the VPC.
- Approve the peering connection request that you received from Yugabyte.
- Add a route table entry to the VPC peer and add the Yugabyte Cloud cluster CIDR block to the Destination column, and the Peering Connection ID to the Target column.

## Deploy a cluster in Yugabyte Cloud

Once the peering connection is active, you can deploy clusters in the VPC. Refer to [Create clusters](../../cloud-basics/create-clusters/).

Before your VPC peer can connect to your cluster, you must:

1. Locate the VPC CIDR block addresses (or subset) associated with the VPC for your cloud provider.
1. Add at least one of these CIDR blocks to the [IP allow list](../../cloud-basics/add-connections) for your Yugabyte Cloud cluster.

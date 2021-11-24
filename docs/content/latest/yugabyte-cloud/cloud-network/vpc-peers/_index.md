---
title: VPC Peering
headerTitle: VPC Peering
linkTitle: VPC Peering
description: Configure VPC peering.
image: /images/section_icons/secure/tls-encryption/connect-to-cluster.png
headcontent: Set up VPC peering for your Yugabyte Cloud clusters.
menu:
  latest:
    identifier: vpc-peers
    parent: cloud-network
    weight: 400
---

Virtual Private Cloud (VPC) peering allows applications running on instances on the same cloud provider as your Yugabyte Cloud clusters to communicate with those clusters without traversing the public internet; all traffic stays within the cloud provider's network. Yugabyte Cloud supports AWC and GCP for self-managed peering. For Azure, contact Yugabyte Cloud support.

To set up peering in Yugabyte Cloud, you first create a VPC to host your clusters, then peer it with the VPC that hosts client applications. Once the peering connection is active, you can deploy a cluster in the VPC you created. Any cluster deployed using that VPC will then be able to connect with any application in the application VPC.

{{< note title="Note" >}}

To use VPC peering in a cluster, your cluster must be deployed in a dedicated VPC that is peered with your application VPC. You must set up a dedicated VPC and peering before deploying your cluster. VPC peering is not supported in free clusters.

{{< /note >}}

## VPC peering

Setting up and peering a VPC works as follows:

- [Create a VPC](#create-a-vpc)
- [Create a Peering Connection](#create-a-peering-connection)
- [Configure the provider](#configure-the-cloud-provider)
- [Deploy a cluster in the VPC](#deploy-a-cluster-in-the-vpc)

### Prerequisites

To set up a VPC in Yugabyte Cloud, you need the details of the VPC you want to peer with. This includes:

- AWS account or GCP project
- VPC ID/network name
- CIDR blocks of the VPC network

### Create a VPC

To create a VPC in Yugabyte Cloud, you need to specify the following:

- Cloud provider (AWS or GCP)
- Region in which to deploy the VPC (AWC only)
- Preferred CIDR to use for your database VPC.

Refer to [Manage VPCs](cloud-add-vpc/).

### Create a peering connection

Once you have created at least one VPC in Yugabyte Cloud, you can create a peering connection with an application VPC on the same cloud provider. You need to specify the following:

- The Yugabyte Cloud VPC to peer.
- Details of the VPC you want to peer with, including:
  - GCP - the project ID and the network name
  - AWS - the AWS account ID, and the VPC ID, region, and CIDR block

Refer to [Manage peering connections](cloud-add-peering/).

### Configure the cloud provider

Once the peering connection is set up, you need to sign in to your cloud provider and configure the connection.

In the Google Cloud Console, this involves creating a peering connection using the project ID and VPC network name.

For AWS, you use the VPC Dashboard to do the following:

- Enable DNS hostnames and DNS resolution. This ensures that the cluster's hostnames in standard connection strings automatically resolve to private instead of public IP addresses when the Yugabyte Cloud cluster is accessed from the VPC.
- Approve the peering connection request that you received from Yugabyte.
- Add a route table entry to the VPC peer and add the Yugabyte Cloud cluster CIDR block to the Destination column, and the Peering Connection ID to the Target column.

### Deploy a cluster in the VPC

Once the peering connection is active, you can deploy clusters in the VPC. Refer to [Create clusters](../../cloud-basics/create-clusters/).

Before your VPC peer can connect to your cluster, you must:

1. Locate the VPC CIDR block addresses (or subset) associated with the VPC for your cloud provider.
1. Add at least one of these CIDR blocks to the [IP allow list](../../cloud-basics/add-connections) for your Yugabyte Cloud cluster.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="./cloud-add-vpc/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Manage VPCs</div>
      </div>
      <div class="body">
        Create VPCs for your cloud.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="./cloud-add-peering/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/create_cluster.png" aria-hidden="true" />
        <div class="title">Manage peering connections</div>
      </div>
      <div class="body">
        Peer your cloud VPCs with an application VPCs.
      </div>
    </a>
  </div>
<!--
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="endpoints/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/edit_universe.png" aria-hidden="true" />
        <div class="title">Manage Endpoints</div>
      </div>
      <div class="body">
        Manage the endpoints for connecting to clusters.
      </div>
    </a>
  </div>
-->
</div>

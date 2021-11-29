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

Virtual Private Cloud (VPC) peering allows applications running on instances on the same cloud provider as your Yugabyte Cloud clusters to communicate with those clusters without traversing the public internet; all traffic stays within the cloud provider's network. This has the following advantages:

- Lower network latency. Traffic uses only internal addresses, which provides lower latency than connectivity that uses external addresses.
- Better security. Your services are never exposed to the public Internet.
- Lower data transfer costs. By staying in the provider's network, you won't have any Internet data transfer traffic. (Same region and cross region overages may still apply. Refer to [Data transfer costs](../../cloud-admin/cloud-billing-costs/#data-transfer-costs).)

Yugabyte Cloud supports AWC and GCP for self-managed peering. For Azure, contact Yugabyte Cloud support.

VPCs and peering connections are managed in Yugabyte Cloud on the **VPC Network** tab of the **Network Access** page.

{{< note title="Note" >}}

To use VPC peering in a cluster, your cluster must be deployed in a dedicated VPC that is peered with your application VPC. You must set up a dedicated VPC _before_ deploying your cluster.

VPC peering is not supported in free clusters.

{{< /note >}}

## VPC peering overview

To set up peering in Yugabyte Cloud, you first create a VPC to host your clusters, then peer it with the VPC that hosts client applications. Some configuration of your cloud provider settings is also required to make the peering connection active. Any cluster deployed using that VPC will then be able to connect with any application in the application VPC.

The following outlines the steps required to peer VPCs in Yugabyte Cloud:

1. **Create a VPC in Yugabyte Cloud**. The VPC is used to host your clusters. To create a VPC in Yugabyte Cloud, you need to specify the following:
    - Cloud provider (AWS or GCP).
    - Region in which to deploy the VPC (AWS only).
    - Preferred CIDR to use for your database VPC.

    Refer to [Create a VPC](cloud-add-vpc/#create-a-vpc).

1. **Deploy a cluster in the VPC**. Once a VPC has been created, you can deploy clusters. When creating the cluster, you specify the VPC under **Network Access** in the Create Cluster wizard.\
    Refer to [Create clusters](../../cloud-basics/create-clusters/).

1. **Create a Peering Connection in Yugabyte Cloud**. Once you have created at least one VPC in Yugabyte Cloud, you can create a peering connection with an application VPC on the same cloud provider. You need to specify the following:
    - The Yugabyte Cloud VPC to peer.
    - Details of the VPC you want to peer with, including:
      - GCP - the project ID and the network name.
      - AWS - the AWS account ID, and the VPC ID, region, and CIDR block.

    Refer to [Create a Peering Connection](cloud-add-peering/#create-a-peering-connection).

1. **Configure the connection in your cloud provider**. Once the peering connection is added in Yugabyte Cloud, you need to sign in to your cloud provider and configure the connection. 
    - In the Google Cloud Console, this involves creating a peering connection using the project ID and network name of the Yugabyte Cloud VPC. 
    - For AWS, you use the VPC Dashboard to accept the peering request, enable DNS, and add a route table entry.

    Refer to [Configure the cloud provider](cloud-add-peering/#configure-the-cloud-provider).

1. **Add the peered application VPC to the IP allow list for your cluster**. Once the cluster and the peering connection are active, you need add at least one of the CIDR blocks associated with the peered application VPC to the IP allow list for your cluster.\
    Refer to [Assign IP allow lists](../../cloud-basics/add-connections).

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

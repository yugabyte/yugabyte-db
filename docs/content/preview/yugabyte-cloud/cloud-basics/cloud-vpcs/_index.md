---
title: VPC network
headerTitle: VPC network
linkTitle: VPC network
description: Configure VPC networking in YugabyteDB Aeon.
image: /images/section_icons/secure/tls-encryption/connect-to-cluster.png
headcontent: Set up VPC networking so that your clusters can communicate privately with applications
aliases:
  - /preview/yugabyte-cloud/cloud-network/vpc-peers/
  - /preview/yugabyte-cloud/cloud-secure-clusters/cloud-vpcs/
menu:
  preview_yugabyte-cloud:
    identifier: cloud-vpcs
    parent: cloud-basics
    weight: 20
type: indexpage
---

A Virtual Private Cloud (VPC) network allows applications running on instances on the same cloud provider as your YugabyteDB Aeon clusters to communicate with those clusters without traversing the public internet; all traffic stays in the cloud provider's network.

Use VPC networks to lower network latencies, make your application and database infrastructure more secure, and reduce network data transfer costs.

In YugabyteDB Aeon, a VPC network consists of the following components:

| Component | Description |
| :--- | :--- |
| [VPC](cloud-add-vpc/) | A VPC reserves a block of IP addresses on the cloud provider.<br />You deploy your cluster in a VPC. |
| [Peering connection](cloud-add-peering/) | Links the cluster VPC to an application VPC on the same cloud provider.<br />AWS and GCP only.<br />A peering connection is created for a VPC.<br />You need to add the IP address of your peered application VPC to the cluster [IP allow list](../../cloud-secure-clusters/add-connections/).<br/>Required for smart load balancing features of [YugabyteDB smart drivers](../../../drivers-orms/smart-drivers/#using-smart-drivers-with-yugabytedb-managed). |
| [Private service endpoint](cloud-add-endpoint/) | Links the cluster endpoint to an application VPC endpoint, using the cloud provider's private linking service.<br />AWS and Azure only.<br />A private service endpoint (PSE) is added to a cluster; the cluster must be deployed in a VPC.<br/>No need to add the IP address of your application to the cluster IP allow list. |

Typically, you would either have a VPC network with peering, or use PSEs.

VPCs and peering connections are managed on the **VPC Network** tab of the **Networking** page.

{{< note title="Note" >}}

To connect a cluster to an application VPC using either a peering connection or a private service endpoint, you need to deploy the cluster in a dedicated VPC. You need to set up the dedicated VPC _before_ deploying your cluster.

VPC networking is not supported in Sandbox clusters.

{{< /note >}}

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="./cloud-vpc-intro/">
      <div class="head">
        <img class="icon" src="/images/section_icons/deploy/public-clouds.png" aria-hidden="true" />
        <div class="title">Overview</div>
      </div>
      <div class="body">
        How to choose the region and size for a VPC.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="./cloud-add-vpc/">
      <div class="head">
        <img class="icon" src="/images/section_icons/index/deploy.png" aria-hidden="true" />
        <div class="title">VPCs</div>
      </div>
      <div class="body">
        Manage your account VPCs.
      </div>
    </a>
  </div>

</div>

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="./cloud-add-peering/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/create_cluster.png" aria-hidden="true" />
        <div class="title">Peering Connections</div>
      </div>
      <div class="body">
        Manage your account peering connections.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="./cloud-add-vpc-aws/">
      <div class="head">
        <img class="icon" src="/images/section_icons/develop/api-icon.png" aria-hidden="true" />
        <div class="title">Peer VPCs</div>
      </div>
      <div class="body">
        Connect your VPC to application VPCs on AWS and GCP using peering.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="./cloud-add-endpoint/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/create_cluster.png" aria-hidden="true" />
        <div class="title">Private service endpoints</div>
      </div>
      <div class="body">
        Manage private service endpoints (PSEs).
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="./managed-endpoint-aws/">
      <div class="head">
        <img class="icon" src="/images/section_icons/develop/api-icon.png" aria-hidden="true" />
        <div class="title">Set up a private link</div>
      </div>
      <div class="body">
        Connect your VPC to application VPCs on AWS and Azure using a private link.
      </div>
    </a>
  </div>

</div>

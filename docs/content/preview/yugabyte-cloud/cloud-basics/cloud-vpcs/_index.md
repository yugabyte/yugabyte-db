---
title: VPC network
headerTitle: VPC network
linkTitle: VPC network
description: Configure VPC networking.
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

A Virtual Private Cloud (VPC) network allows applications running on instances on the same cloud provider as your YugabyteDB Managed clusters to communicate with those clusters without traversing the public internet; all traffic stays in the cloud provider's network.

Use VPC networks to lower network latencies, make your application and database infrastructure more secure, and reduce network data transfer costs.

In YugabyteDB Managed, a VPC network consists of the following:

- The _VPC_, where you can deploy clusters. The VPC reserves a block of IP addresses on the cloud provider.
- A _peering connection_, which links the cluster VPC to an application VPC on the same cloud provider.

VPCs and peering connections are managed on the **VPC Network** tab of the **Network Access** page.

{{< note title="Note" >}}

To peer a cluster with an application VPC, you need to deploy the cluster in a dedicated VPC. You need to set up the dedicated VPC _before_ deploying your cluster.

VPC peering is not supported in Sandbox clusters.

{{< /note >}}

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="./cloud-vpc-intro/">
      <div class="head">
        <img class="icon" src="/images/section_icons/deploy/public-clouds.png" aria-hidden="true" />
        <div class="title">Overview</div>
      </div>
      <div class="body">
        VPC networking in YugabyteDB Managed.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="./cloud-add-vpc-aws/">
      <div class="head">
        <img class="icon" src="/images/section_icons/develop/api-icon.png" aria-hidden="true" />
        <div class="title">Create a VPC network</div>
      </div>
      <div class="body">
        Create a VPC network on AWS and GCP.
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
        Manage VPCs for your clusters.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="./cloud-add-peering/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/create_cluster.png" aria-hidden="true" />
        <div class="title">Peering Connections</div>
      </div>
      <div class="body">
        Manage peering connections to application VPCs.
      </div>
    </a>
  </div>

</div>

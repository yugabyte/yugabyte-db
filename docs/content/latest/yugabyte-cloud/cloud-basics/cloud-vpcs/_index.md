---
title: VPC network
headerTitle: VPC network
linkTitle: VPC network
description: Configure VPC networking.
image: /images/section_icons/secure/tls-encryption/connect-to-cluster.png
headcontent: Set up VPC networking so that your clusters can communicate privately with applications.
aliases:
  - /latest/yugabyte-cloud/cloud-network/vpc-peers/
menu:
  latest:
    identifier: cloud-vpcs
    parent: cloud-secure-clusters
    weight: 200
---

A Virtual Private Cloud (VPC) network allows applications running on instances on the same cloud provider as your Yugabyte Cloud clusters to communicate with those clusters without traversing the public internet; all traffic stays within the cloud provider's network.

In Yugabyte Cloud, a VPC network consists of a _VPC_ where you can deploy clusters, and a _peering connection_ that links the cluster VPC to an application VPC on the same cloud provider.

Use VPC networks to lower network latencies, make your application and database infrastructure more secure, and reduce network data transfer costs.

VPCs and peering connections are managed in Yugabyte Cloud on the **VPC Network** tab of the **Network Access** page.

{{< note title="Note" >}}

To use VPC peering in a cluster, your cluster must be deployed in a dedicated VPC that is peered with your application VPC. You must set up a dedicated VPC _before_ deploying your cluster.

VPC peering is not supported in free clusters.

{{< /note >}}

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="./cloud-vpc-intro/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Overview</div>
      </div>
      <div class="body">
        VPC networking in Yugabyte Cloud.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="./cloud-vpc-setup/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Set up a VPC network</div>
      </div>
      <div class="body">
        How to set up a VPC network in Yugabyte Cloud.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="./cloud-add-vpc/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">VPCs</div>
      </div>
      <div class="body">
        Create and manage VPCs for your clusters.
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
        Create and manage peering connections to application VPCs.
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

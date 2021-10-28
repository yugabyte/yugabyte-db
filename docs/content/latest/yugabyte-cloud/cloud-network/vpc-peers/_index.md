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

Virtual Private Cloud (VPC) peering allows applications running on instances on the same cloud provider as your Yugabyte Cloud cluster to communicate with your YugabyteDB clusters without traversing the public internet - traffic stays within the cloud provider's network.

{{< note title="Note" >}}

To use VPC peering in a cluster, your cluster must be deployed in a dedicated VPC that is peered with your application VPC. You must set up dedicated VPCs and peering before deploying your cluster. VPC peering is not supported in Free clusters.

{{< /note >}}

Setting up a VPC peering connection between your cluster and a client application VPC is a two-part process:

- Add the VPC. This involves setting a CIDR IP range for the peering.
- Configure a peering connection between your VPC and the application VPC.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="cloud-vpcs">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Introduction</div>
      </div>
      <div class="body">
        Configure VPC peering to allow applications running on other cloud instances to communicate with your YugabyteDB clusters.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="./add-vpcs/aws">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Add VPC peers</div>
      </div>
      <div class="body">
        Create VPC peers for your cloud.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="./add-peering/aws/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/create_cluster.png" aria-hidden="true" />
        <div class="title">Set up peering connections</div>
      </div>
      <div class="body">
        Create a peering connection between your cloud VPC and your application VPC.
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

---
title: Deploy clusters in Yugabyte Cloud
linkTitle: Deploy clusters
description: Deploy clusters in Yugabyte Cloud.
headcontent: Deploy production-ready clusters.
image: /images/section_icons/index/quick_start.png
menu:
  latest:
    identifier: cloud-basics
    parent: yugabyte-cloud
    weight: 25
isTocNested: true
showAsideToc: true
---

Using Yugabyte Cloud, you can create single region clusters that can be deployed across multiple and single availability zones.

You deploy clusters in Yugabyte Cloud using the **Create Cluster** wizard. Before deploying a cluster, you need to decide on the following:

- Cloud provider and regions - Where you deploy the cluster will impact latencies. For a list of currently supported regions in Yugabyte Cloud, refer to [Cloud provider regions](../release-notes/#cloud-provider-regions).
- Cluster size - Size your cluster to the potential load. As Yugabyte Cloud supports both horizontal and vertical scaling, you can always change this later to better match your performance requirements.
- Fault Tolerance - The fault tolerance determines how resilient the cluster is to node and cloud zone failures. You cannot change the fault tolerance after the cluster is created.
- VPC peering - Virtual Private Cloud (VPC) peering allows applications running on instances on the same cloud provider as your Yugabyte Cloud cluster to communicate with your cluster without traversing the public internet. If you want your cluster to be in a dedicated VPC and to be peered with an application VPC, you must set up the VPC before you create your cluster.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="create-clusters/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/create_cluster.png" aria-hidden="true" />
        <div class="title">Create clusters</div>
      </div>
      <div class="body">
        Create a new cluster in Yugabyte Cloud.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="add-connections/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/tls-encryption/connect-to-cluster.png" aria-hidden="true" />
        <div class="title">IP allow lists</div>
      </div>
      <div class="body">
        Whitelist IP addresses to control who can connect to your clusters.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="add-extensions/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/administer.png" aria-hidden="true" />
        <div class="title">Create extensions</div>
      </div>
      <div class="body">
        Create PostgreSQL extensions in Yugabyte Cloud clusters.
      </div>
    </a>
  </div>

<!--
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="vpc-peers/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/create_cluster.png" aria-hidden="true" />
        <div class="title">VPC peering</div>
      </div>
      <div class="body">
        Add VPC peers to allow applications running on other cloud instances to communicate with your Yugabyte Cloud clusters.
      </div>
    </a>
  </div>
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

---
title: Deploy clusters in YugabyteDB Managed
linkTitle: Deploy clusters
description: Deploy clusters in YugabyteDB Managed.
headcontent: Deploy production-ready clusters
image: /images/section_icons/index/quick_start.png
menu:
  preview_yugabyte-cloud:
    parent: yugabytedb-managed
    identifier: cloud-basics
    weight: 25
type: indexpage
---

Deploy single- and multi-region production clusters on Amazon Web Services (AWS), Microsoft Azure, and Google Cloud Platform (GCP).

### Prepare

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="create-clusters-overview/">
      <div class="head">
        <img class="icon" src="/images/section_icons/introduction/benefits.png" aria-hidden="true" />
        <div class="title">Plan your cluster</div>
      </div>
      <div class="body">
        Plan your production deployment.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="create-clusters-topology/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/planet_scale.png" aria-hidden="true" />
        <div class="title">Choose a topology</div>
      </div>
      <div class="body">
        Choose the topology best suited to your requirements for latency, availability, and geo-distribution.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="cloud-vpcs/">
      <div class="head">
         <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">VPC networks</div>
      </div>
      <div class="body">
        Connect clusters privately to other resources on the same cloud provider.
      </div>
    </a>
  </div>

</div>

### Deploy

#### Single region

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="create-clusters/create-single-region/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/create_cluster.png" aria-hidden="true" />
        <div class="title">Single region multi zone</div>
      </div>
      <div class="body">
        Deploy single-region clusters with node- and availability zone-level fault tolerance.
      </div>
    </a>
  </div>

</div>

#### Multi region

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="create-clusters/create-clusters-multisync/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/planet_scale.png" aria-hidden="true" />
        <div class="title">Replicate across regions</div>
      </div>
      <div class="body">
        Create a stretched multi-region synchronous cluster.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="create-clusters/create-clusters-geopartition/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/planet_scale.png" aria-hidden="true" />
        <div class="title">Partition by region</div>
      </div>
      <div class="body">
        Use geo-partitioning to pin data to specific geographical regions.
      </div>
    </a>
  </div>

</div>

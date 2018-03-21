---
title: Configure Cloud Providers
linkTitle: 3. Configure Cloud Providers
description: Configure Cloud Providers
aliases:
  - /deploy/enterprise-edition/configure-cloud-providers/
menu:
  latest:
    identifier: configure-cloud-providers
    parent: deploy-enterprise-edition
    weight: 680
---

This section details how to configure cloud providers for YugaByte DB using the YugaWare Admin Console. If no cloud providers are configured in YugaWare yet, the main Dashboard page highlights the need to configure at least 1 cloud provider.

![Configure Cloud Provider](/images/ee/configure-cloud-provider.png)

## Prerequisites

### Public cloud

If you plan to run YugaByte DB nodes on public cloud providers such as Amazon Web Services (AWS) or Google Cloud Platform (GCP), all you need to provide on YugaWare UI is your cloud provider credentials. YugaWare will use those credentials to automatically provision and de-provision instances that run YugaByte. An 'instance' for YugaByte includes a compute instance as well as local or remote disk storage attached to the compute instance.

### Private cloud or on-premises data centers

The prerequisites here are same as that of the [Community Edition](../../multi-node-cluster/#prerequisites).

## Configure your cloud provider in YugaWare

<ul class="nav nav-tabs nav-tabs-yb">
  <li class="active">
    <a href="#configure-aws">
      <i class="icon-aws"></i>
      AWS
    </a>
  </li>
  <li>
    <a href="#configure-gcp">
      <i class="icon-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>
  <li>
    <a href="#configure-azure">
      <i class="icon-google" aria-hidden="true"></i>
      Azure
    </a>
  </li>
  <li>
    <a href="#configure-docker">
      <i class="icon-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>
  <li>
    <a href="#configure-onprem">
      <i class="fa fa-cubes" aria-hidden="true"></i>
      On-Premises
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="configure-aws" class="tab-pane fade in active">
    {{% includeMarkdown "configure-cloud/aws.md" /%}}
  </div>
  <div id="configure-gcp" class="tab-pane fade in active">
    {{% includeMarkdown "configure-cloud/gcp.md" /%}}
  </div>
  <div id="configure-azure" class="tab-pane fade in active">
    {{% includeMarkdown "configure-cloud/azure.md" /%}}
  </div>
  <div id="configure-docker" class="tab-pane fade in active">
    {{% includeMarkdown "configure-cloud/docker.md" /%}}
  </div>
  <div id="configure-onprem" class="tab-pane fade in active">
    {{% includeMarkdown "configure-cloud/onprem.md" /%}}
  </div>
</div>

## Next Step

You are now ready to create YugaByte DB universes as outlined  in the [next section](../../../manage/enterprise-edition/create-universe/).

---
title: Configure Cloud Providers
linkTitle: 4. Configure Cloud Providers
description: Configure Cloud Providers
menu:
  v1.0:
    identifier: configure-cloud-providers
    parent: deploy-enterprise-edition
    weight: 680
---

This section details how to configure cloud providers for YugaByte DB using the YugaWare Admin Console. If no cloud providers are configured in YugaWare yet, the main Dashboard page highlights the need to configure at least 1 cloud provider.

![Configure Cloud Provider](/images/ee/configure-cloud-provider.png)

## Prerequisites

### Public cloud

If you plan to run YugaByte DB nodes on public cloud providers such as Amazon Web Services (AWS) or Google Cloud Platform (GCP), all you need to provide on YugaWare UI is your cloud provider credentials. YugaWare will use those credentials to automatically provision and de-provision instances that run YugaByte. An 'instance' for YugaByte includes a compute instance as well as local or remote disk storage attached to the compute instance.

### Private cloud or on-premises datacenters

The prerequisites for YugaByte DB Enterprise Edition data nodes are same as that of [YugaByte DB Community Edition](../../checklist/).

## Configure cloud providers

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#configure-aws" class="nav-link active" id="configure-aws-tab" data-toggle="tab" role="tab" aria-controls="configure-aws" aria-selected="true">
      <i class="icon-aws"></i>
      AWS
    </a>
  </li>
  <li>
    <a href="#configure-gcp" class="nav-link" id="configure-gcp-tab" data-toggle="tab" role="tab" aria-controls="configure-gcp" aria-selected="false">
      <i class="icon-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>
  <li>
    <a href="#configure-azure" class="nav-link" id="configure-azure-tab" data-toggle="tab" role="tab" aria-controls="configure-azure" aria-selected="false">
      <i class="icon-azure" aria-hidden="true"></i>
       Azure
    </a>
  </li>
  <li>
    <a href="#configure-k8s" class="nav-link" id="configure-k8s-tab" data-toggle="tab" role="tab" aria-controls="configure-k8s" aria-selected="false">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
  <li>
    <a href="#configure-onprem" class="nav-link" id="configure-onprem-tab" data-toggle="tab" role="tab" aria-controls="configure-onprem" aria-selected="false">
      <i class="fas fa-building"></i>
      On-Premises
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="configure-aws" class="tab-pane fade show active" role="tabpanel" aria-labelledby="configure-aws-tab">
    {{% includeMarkdown "configure-cloud/aws.md" /%}}
  </div>
  <div id="configure-gcp" class="tab-pane fade" role="tabpanel" aria-labelledby="configure-gcp-tab">
    {{% includeMarkdown "configure-cloud/gcp.md" /%}}
  </div>
  <div id="configure-azure" class="tab-pane fade" role="tabpanel" aria-labelledby="configure-azure-tab">
    {{% includeMarkdown "configure-cloud/azure.md" /%}}
  </div>
  <div id="configure-k8s" class="tab-pane fade" role="tabpanel" aria-labelledby="configure-k8s-tab">
    {{% includeMarkdown "configure-cloud/kubernetes.md" /%}}
  </div>
  <div id="configure-onprem" class="tab-pane fade" role="tabpanel" aria-labelledby="configure-onprem-tab">
    {{% includeMarkdown "configure-cloud/onprem.md" /%}}
  </div>
</div>

## Next step

You are now ready to create YugaByte DB universes as outlined in the next section.

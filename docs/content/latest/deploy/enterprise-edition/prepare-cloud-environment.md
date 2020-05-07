---
title: Prepare cloud environments for Yugabyte Platform and YugaWare
headerTitle: Prepare cloud environment
linkTitle: 1. Prepare cloud environment
description: Prepare your cloud environment for the Yugabyte Platform
aliases:
  - /deploy/enterprise-edition/prepare-cloud-environment/
menu:
  latest:
    identifier: prepare-cloud-environment
    parent: deploy-enterprise-edition
    weight: 669
---

A dedicated host or virtual machine (VM) is needed to run YugaWare. For more details, see [this faq](../../../faq/enterprise-edition/#what-are-the-os-requirements-and-permissions-to-run-yugaware-the-yugabyte-admin-console). This page highlights the basic setup needed in order to install YugaWare.

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#prepare-cloud-aws" class="nav-link active" id="prepare-cloud-aws-tab" data-toggle="tab" role="tab" aria-controls="prepare-cloud-aws" aria-selected="true">
      <i class="icon-aws" aria-hidden="true"></i>
      AWS
    </a>
  </li>
  <li>
    <a href="#prepare-cloud-gcp" class="nav-link" id="prepare-cloud-gcp-tab" data-toggle="tab" role="tab" aria-controls="prepare-cloud-gcp" aria-selected="true">
      <i class="icon-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="prepare-cloud-aws" class="tab-pane fade show active" role="tabpanel" aria-labelledby="prepare-cloud-aws-tab">
    {{% includeMarkdown "prepare-cloud/aws.md" /%}}
  </div>
  <div id="prepare-cloud-gcp" class="tab-pane fade" role="tabpanel" aria-labelledby="prepare-cloud-gcp-tab">
    {{% includeMarkdown "prepare-cloud/gcp.md" /%}}
  </div>
</div>

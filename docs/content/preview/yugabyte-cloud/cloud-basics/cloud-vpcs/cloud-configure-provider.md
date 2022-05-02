---
title: Configure the cloud provider
headerTitle:
linkTitle: Configure provider
description: Configure your application VPC cloud provider to peer with your VPCs.
menu:
  preview:
    identifier: cloud-configure-provider
    parent: cloud-vpcs
    weight: 50
isTocNested: true
showAsideToc: true
---

A peering connection connects a YugabyteDB Managed VPC with a VPC on the corresponding cloud provider - typically one that hosts an application that you want to have access to your cluster. **Peering Connections** on the **VPC Network** tab displays a list of peering connections configured for your cloud.

If a peering connection is _Pending_, you need to configure the corresponding cloud provider, as follows:

- In AWS, accept the peering request.
- In GCP, create a peering connection.

## Configure the cloud provider

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#provider-aws" class="nav-link active" id="provider-aws-tab" data-toggle="tab" role="tab" aria-controls="provider-aws" aria-selected="true">
      <i class="fab fa-aws" aria-hidden="true"></i>
      AWS
    </a>
  </li>
  <li>
    <a href="#provider-gcp" class="nav-link" id="provider-gcp-tab" data-toggle="tab" role="tab" aria-controls="provider-gcp" aria-selected="false">
      <i class="fab fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="provider-aws" class="tab-pane fade show active" role="tabpanel" aria-labelledby="provider-aws-tab">
    {{% includeMarkdown "provider/provider-aws.md" /%}}
  </div>
  <div id="provider-gcp" class="tab-pane fade" role="tabpanel" aria-labelledby="provider-gcp-tab">
    {{% includeMarkdown "provider/provider-gcp.md" /%}}
  </div>
</div>

## Next steps

- [Add the application VPC CIDR to the cluster IP allow list](../../../cloud-secure-clusters/add-connections/)

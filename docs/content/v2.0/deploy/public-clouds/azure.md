---
title: Microsoft Azure
linkTitle: Microsoft Azure
description: Microsoft Azure
block_indexing: true
menu:
  v2.0:
    identifier: deploy-in-azure
    parent: public-clouds
    weight: 650
---

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#terraform" class="nav-link active" id="terraform-tab" data-toggle="tab" role="tab" aria-controls="terraform" aria-selected="true">
      <i class="icon-shell"></i>
      Terraform
    </a>
  </li>
  <li>
    <a href="#azure-arm" class="nav-link" id="azure-arm-tab" data-toggle="tab" role="tab" aria-controls="terraform" aria-selected="true">
      <i class="icon-shell"></i>
      Azure ARM Template
    </a>
  </li>
  <li>
    <a href="#aks" class="nav-link" id="aks-tab" data-toggle="tab" role="tab" aria-controls="aks" aria-selected="true">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Azure Kubernetes Service (AKS)
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="terraform" class="tab-pane fade" role="tabpanel" aria-labelledby="terraform-tab">
    {{% includeMarkdown "azure/terraform.md" /%}}
  </div>
  <div id="azure-arm" class="tab-pane fade" role="tabpanel" aria-labelledby="azure-arm-tab">
    {{% includeMarkdown "azure/azure-arm.md" /%}}
  </div>
    <div id="aks" class="tab-pane fade show active" role="tabpanel" aria-labelledby="aks-tab">
    {{% includeMarkdown "azure/aks.md" /%}}
  </div>
</div>

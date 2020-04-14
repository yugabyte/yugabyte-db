---
title: Microsoft Azure
linkTitle: Microsoft Azure
description: Microsoft Azure
block_indexing: true
menu:
  v1.3:
    identifier: deploy-in-azure
    parent: public-clouds
    weight: 650
---

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#aks" class="nav-link active" id="aks-tab" data-toggle="tab" role="tab" aria-controls="aks" aria-selected="true">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Azure Container Service (AKS)
    </a>
  </li>
  <li>
    <a href="#terraform" class="nav-link" id="terraform-tab" data-toggle="tab" role="tab" aria-controls="terraform" aria-selected="true">
      <i class="icon-shell"></i>
      Terraform
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="aks" class="tab-pane fade show active" role="tabpanel" aria-labelledby="aks-tab">
    {{% includeMarkdown "azure/aks.md" /%}}
  </div>
  <div id="terraform" class="tab-pane fade" role="tabpanel" aria-labelledby="terraform-tab">
    {{% includeMarkdown "azure/terraform.md" /%}}
  </div>
</div>

---
title: Google Cloud Platform
linkTitle: Google Cloud Platform
description: Google Cloud Platform
block_indexing: true
menu:
  v1.2:
    identifier: deploy-in-gcp
    parent: public-clouds
    weight: 640
---

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#gke" class="nav-link active" id="gke-tab" data-toggle="tab" role="tab" aria-controls="gke" aria-selected="true">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Google Kubernetes Engine (GKE)
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
  <div id="gke" class="tab-pane fade show active" role="tabpanel" aria-labelledby="gke-tab">
    {{% includeMarkdown "gcp/gke.md" /%}}
  </div>
  <div id="terraform" class="tab-pane fade" role="tabpanel" aria-labelledby="terraform-tab">
    {{% includeMarkdown "gcp/terraform.md" /%}}
  </div>
</div>

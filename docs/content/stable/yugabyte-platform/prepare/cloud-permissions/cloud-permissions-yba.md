---
title: Cloud setup for deploying YugabyteDB Anywhere
headerTitle: To deploy YugabyteDB Anywhere
linkTitle: To deploy YBA
description: Prepare your cloud for deploying YugabyteDB Anywhere.
headContent: Prepare your cloud for deploying YugabyteDB Anywhere
menu:
  stable_yugabyte-platform:
    identifier: cloud-permissions-yba
    parent: cloud-permissions
    weight: 10
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#onprem" class="nav-link active" id="onprem-tab" data-bs-toggle="tab"
      role="tab" aria-controls="onprem" aria-selected="true">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>
  <li>
    <a href="#aws" class="nav-link" id="aws-tab" data-bs-toggle="tab"
      role="tab" aria-controls="aws" aria-selected="false">
      <i class="fa-brands fa-aws"></i>
      AWS
    </a>
  </li>
  <li>
    <a href="#gcp" class="nav-link" id="gcp-tab" data-bs-toggle="tab"
      role="tab" aria-controls="gcp" aria-selected="false">
      <i class="fa-brands fa-google"></i>
      GCP
    </a>
  </li>
  <li>
    <a href="#azure" class="nav-link" id="azure-tab" data-bs-toggle="tab"
      role="tab" aria-controls="azure" aria-selected="false">
      <i class="fa-brands fa-microsoft"></i>
      Azure
    </a>
  </li>
  <li>
    <a href="#k8s" class="nav-link" id="k8s-tab" data-bs-toggle="tab"
      role="tab" aria-controls="k8s" aria-selected="false">
      <i class="fa-regular fa-dharmachakra"></i>
      Kubernetes
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="onprem" class="tab-pane fade show active" role="tabpanel" aria-labelledby="onprem-tab">

When installing YugabyteDB Anywhere on a server for on-premises providers, no cloud permissions are required.

Linux OS root permissions are required for the server, see [Servers for YBA](../../server-yba/).

  </div>

  <div id="aws" class="tab-pane fade" role="tabpanel" aria-labelledby="aws-tab">

When installing YugabyteDB Anywhere on an AWS VM, no cloud permissions are required.

Linux OS root permissions are required for the server, see [Servers for YBA](../../server-yba/).

  </div>

  <div id="gcp" class="tab-pane fade" role="tabpanel" aria-labelledby="gcp-tab">

When installing YugabyteDB Anywhere on a GCP VM, no cloud permissions are required.

Linux OS root permissions are required for the server, see [Servers for YBA](../../server-yba/).

  </div>

  <div id="azure" class="tab-pane fade" role="tabpanel" aria-labelledby="azure-tab">

When installing YugabyteDB Anywhere on an Azure VM, no cloud permissions are required.

Linux OS root permissions are required for the server, see [Servers for YBA](../../server-yba/).

  </div>

  <div id="k8s" class="tab-pane fade" role="tabpanel" aria-labelledby="k8s-tab">

Installing YugabyteDB Anywhere to a Kubernetes pod requires a service account that has a RoleBinding to an admin role for at least one namespace. This is required to run the Helm commands necessary to install YBA.

Additionally, the default Helm chart will attempt to create a service account that has certain ClusterRoles listed [here](https://github.com/yugabyte/charts/blob/master/stable/yugaware/templates/rbac.yaml). These roles are used to do the following:

1. Enable YBA to collect resource metrics such as CPU and memory from the Kubernetes nodes.
1. Create YugabyteDB deployments in new namespaces.

If you cannot allow the creation of this service account, you can disable the automatic creation and instead specify a pre-created service account for YBA to use. We recommend that you grant this service account the cluster roles listed in the "required to scrape" section [in this file](https://github.com/yugabyte/charts/blob/master/stable/yugaware/templates/rbac.yaml#L166) along with a namespace admin role.

  </div>

</div>

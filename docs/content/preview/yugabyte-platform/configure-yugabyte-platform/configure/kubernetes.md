---
title: Configure YugabyteDB Overview
headerTitle: Overview
linkTitle: Overview
description: Configuring YugabyteDB Anywhere on Kubernetes
image: /images/section_icons/deploy/enterprise.png
headcontent: Configuring YugabyteDB Anywhere on Kubernetes
menu:
  preview_yugabyte-platform:
    identifier: configure-4-k8s
    parent: configure-yugabyte-platform
    weight: 5
type: docs
---

For overview of how to configure database nodes, select one of the following cloud providers:

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../aws/" class="nav-link">
      <i class="fab fa-aws"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../gcp/" class="nav-link">
      <i class="fab fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="../azure/" class="nav-link">
      <i class="fab fa-windows" aria-hidden="true"></i>
      Azure
    </a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="../onprem/" class="nav-link">
      <i class="fas fa-building"></i>
      On-premises
    </a>
  </li>

</ul>

Click elements of the following chart to access detailed steps:

<img src="/images/ee/flowchart/yb-configure-k8s.png" usemap="#image-map">

<map name="image-map">
    <area target="_blank" alt="Configure cloud provider" title="Configure cloud provider" href="/preview/yugabyte-platform/configure-yugabyte-platform/" coords="390,70,521,192" shape="rect">
    <area target="_blank" alt="create admin user" title="create admin user" href="/preview/yugabyte-platform/configure-yugabyte-platform/create-admin-user/" coords="286,259,617,319" shape="rect">
    <area target="_blank" alt="configure K8s provider" title="configure K8s provider" href="/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/kubernetes/" coords="230,369,666,426" shape="rect">
    <area target="_blank" alt="K8s pre reqs" title="K8s pre reqs" href="/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/kubernetes/#prerequisites" coords="225,475,679,613" shape="rect">
    <area target="_blank" alt="K8s cloud" title="K8s cloud" href="/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/kubernetes/#configure-the-cloud-provider" coords="304,670,599,758" shape="rect">
</map>

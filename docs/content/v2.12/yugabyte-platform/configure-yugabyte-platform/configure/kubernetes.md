---
title: Configure Kubernetes
headerTitle: Overview
linkTitle: Overview
description: Configuring Yugabyte Platform on Kubernetes
image: /images/section_icons/deploy/enterprise.png
headcontent: Configuring Yugabyte Platform on Kubernetes
menu:
  v2.12_yugabyte-platform:
    identifier: configure-4-k8s
    parent: configure-yugabyte-platform
    weight: 5
type: docs
---

Select your cloud provider to see the steps for configuration of database nodes. Click on the elements to see detailed steps.

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../aws" class="nav-link">
      <i class="fab fa-aws"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../gcp" class="nav-link">
      <i class="fab fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="../azure" class="nav-link">
      <i class="fab fa-windows" aria-hidden="true"></i>
      Azure
    </a>
  </li>

  <li>
    <a href="../kubernetes" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="../onprem" class="nav-link">
      <i class="fas fa-building"></i>
      On-premises
    </a>
  </li>

</ul>

<div class="image-with-map">
<img src="/images/ee/flowchart/yb-configure-k8s.png" usemap="#image-map">

<map name="image-map">
    <area alt="Configure cloud provider" title="Configure cloud provider" href="/preview/yugabyte-platform/configure-yugabyte-platform/" coords="390,70,521,192" shape="rect" style=" width: 18%; height: 17%; top: 5%; left: 41%;">
    <area alt="create admin user" title="create admin user" href="/preview/yugabyte-platform/configure-yugabyte-platform/create-admin-user/" coords="286,259,617,319" shape="rect" style=" width: 38%; height: 6%; top: 27%; left: 31%; ">
    <area alt="configure K8s provider" title="configure K8s provider" href="/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/kubernetes/" coords="230,369,666,426" shape="rect" style=" width: 50%; height: 7%; top: 38%; left: 25%; ">
    <area alt="K8s pre reqs" title="K8s pre reqs" href="/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/kubernetes/#prerequisites" coords="225,475,679,613" shape="rect" style="width: 50%;height: 15%;top: 49%;left: 25%;">
    <area alt="K8s cloud" title="K8s cloud" href="/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/kubernetes/#configure-the-cloud-provider" coords="304,670,599,758" shape="rect" style="top: 69%;height: 10%;left: 33%;width: 34%;">
</map>
</div>

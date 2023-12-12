---
title: Configure YugabyteDB Anywhere on Kubernetes
headerTitle: Overview
linkTitle: Overview
description: Configure YugabyteDB Anywhere on Kubernetes
headcontent: Configure YugabyteDB Anywhere on Kubernetes
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
      <i class="fa-brands fa-aws"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../gcp/" class="nav-link">
      <i class="fa-brands fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="../azure/" class="nav-link">
      <i class="fa-brands fa-windows" aria-hidden="true"></i>
      Azure
    </a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link active">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="../onprem/" class="nav-link">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>

</ul>

The following diagram depicts the configuration process for Kubernetes.

<div class="image-with-map">
<img src="/images/ee/flowchart/yb-configure-k8s.png" usemap="#image-map">

<map name="image-map">
    <area alt="create admin user" title="create admin user" href="../../create-admin-user/" coords="286,259,617,319" shape="rect" style=" width: 38%; height: 6%; top: 27%; left: 31%; ">
    <area alt="configure K8s provider" title="configure K8s provider" href="../../set-up-cloud-provider/kubernetes/" coords="230,369,666,426" shape="rect" style=" width: 50%; height: 7%; top: 38%; left: 25%; ">
    <area alt="K8s pre reqs" title="K8s pre reqs" href="../../set-up-cloud-provider/kubernetes/#prerequisites" coords="225,475,679,613" shape="rect" style="width: 50%;height: 15%;top: 49%;left: 25%;">
    <area alt="K8s cloud" title="K8s cloud" href="../../set-up-cloud-provider/kubernetes/#configure-the-cloud-provider" coords="304,670,599,758" shape="rect" style="top: 69%;height: 10%;left: 33%;width: 34%;">
</map>
</div>

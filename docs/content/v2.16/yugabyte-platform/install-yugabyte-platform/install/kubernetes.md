---
title: Install YugabyteDB Anywhere Overview
headerTitle: Overview
linkTitle: Overview
description: Installing YugabyteDB Anywhere on Kubernetes
image: /images/section_icons/deploy/enterprise.png
menu:
  v2.16_yugabyte-platform:
    identifier: install-2-k8s
    parent: install-yugabyte-platform
    weight: 20
type: docs
---

For installation overview, select one of the following installation types:

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../public-cloud/" class="nav-link">
      <i class="fa-solid fa-cloud"></i>
      Public Cloud
    </a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link active">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li >
    <a href="../private-cloud/" class="nav-link">
      <i class="fa-solid fa-link-slash"></i>
      Private Cloud
    </a>
  </li>
</ul>

The following diagram depicts the YugabyteDB Anywhere installation process in the Kubernetes environment:

<div class="image-with-map">
<img src="/images/ee/flowchart/yb-install-k8s.png" usemap="#image-map">

<map name="image-map">
    <area alt="K8s pre-reqs" title="K8s pre-reqs" href="../../prepare-environment/kubernetes/" coords="323,257,576,496" shape="rect" style="top: 16%;height: 15.2%;left: 36%;width: 28%;">
    <area alt="Install K8s" title="Install K8s" href="../../install-software/kubernetes/#install-yugabyte-platform-on-a-kubernetes-cluster" coords="346,1032,551,1166" shape="rect" style="top: 64%;height: 9%;left: 38%;width: 24%;">
</map>
</div>

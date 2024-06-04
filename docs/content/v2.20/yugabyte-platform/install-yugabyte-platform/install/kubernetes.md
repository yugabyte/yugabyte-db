---
title: Install YugabyteDB Anywhere on Kubernetes Overview
headerTitle: Overview
linkTitle: Overview
description: Installing YugabyteDB Anywhere on Kubernetes
menu:
  v2.20_yugabyte-platform:
    identifier: install-2-k8s
    parent: install-yugabyte-platform
    weight: 20
type: docs
---

Before you get started, decide whether you are deploying on Public cloud, On-premises, or Kubernetes. In public cloud, YugabyteDB Anywhere (YBA) creates and launches virtual machine (VM) instances on the cloud to become nodes in a YugabyteDB universe, and YBA needs permissions to create VMs. In On-premises, you manually create VMs, and then provide the hostnames of these VMs to YBA, where they become "free nodes" to be used in creating universes.

All of these process flows follow the same general steps:

- Install and configure YugabyteDB Anywhere.
- Create a Provider Configuration (choose among AWS, GCP, Azure, on-prem, or various Kubernetes options).
- Create a Universe using the configuration.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../public-cloud/" class="nav-link">
      <i class="fa-solid fa-cloud"></i>
      Public Cloud
    </a>
  </li>

  <li >
    <a href="../private-cloud/" class="nav-link">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link active">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>

The following diagram shows the YugabyteDB Anywhere installation process in a Kubernetes environment. Click elements of the chart to access detailed steps.

<div class="image-with-map">
<img src="/images/ee/flowchart/yb-install-k8s.png" usemap="#image-map">

<map name="image-map">
    <area alt="K8s pre-reqs" title="K8s pre-reqs" href="../../prepare-environment/kubernetes/" coords="323,257,576,496" shape="rect" style="top: 16%;height: 15.2%;left: 36%;width: 28%;">
    <area alt="Install K8s" title="Install K8s" href="../../install-software/kubernetes/#install-yugabyte-platform-on-a-kubernetes-cluster" coords="346,1032,551,1166" shape="rect" style="top: 64%;height: 9%;left: 38%;width: 24%;">
</map>
</div>

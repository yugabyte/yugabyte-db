---
title: Install YugabyteDB Anywhere on Public Cloud Overview
headerTitle: Overview
linkTitle: Overview
description: Installing YugabyteDB Anywhere on public clouds
aliases:
  - /preview/yugabyte-platform/overview/install/
menu:
  preview_yugabyte-platform:
    identifier: install-1-public-cloud
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
    <a href="../public-cloud/" class="nav-link active">
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
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>

The following diagram shows the YugabyteDB Anywhere installation process in a public cloud. Click elements of the chart to access detailed steps.

Note: If you are using a public cloud such as AWS, GCP, or Azure, but you cannot grant YBA permissions (or a service account) to create VMs on your public cloud, use the On-premises workflow instead. In this case you would manually create VMs on your public cloud, and then provide the hostnames of these VMs to YBA.

<div class="image-with-map">
<img src="/images/ee/flowchart/yb-install-public-cloud.png" usemap="#image-map">

<map name="image-map">
    <area alt="AWS prep environment" title="AWS prep environment" href="../../prepare-environment/aws/" coords="166,404,296,480" shape="rect" style="width: 16.5%;height: 4.1%;top: 21.2%;left: 17.5%;">
    <area alt="GCP prep environment" title="GCP prep environment" href="../../prepare-environment/gcp/" coords="378,404,521,480" shape="rect" style="width: 16.5%;height: 4.1%;top: 21.2%;left: 41.8%;">
    <area alt="Azure prep environment" title="Azure prep environment" href="../../prepare-environment/azure/" coords="590,404,746,480" shape="rect" style="width: 16.5%;height: 4.1%;top: 21.2%;left: 66%;">
    <area alt="Prerequisites" title="Prerequisites" href="../../prerequisites/installer/" coords="320,555,581,722" shape="rect" style="width: 30%;height: 9%;top: 29%;left: 35%;">
    <area alt="Prepare environment" title="Prepare environment" href="../../prepare-environment/aws/" coords="324,558,574,711" shape="rect" style="width: 81%;height: 3.2%;top: 40.3%;left: 9.5%;">
    <area alt="Online installation" title="Online installation" href="../../install-software/installer/" coords="236,1054,394,1112" shape="rect" style="width: 19%;height: 3.4%;top: 55.4%;left: 25.5%;">
    <area alt="Airgapped installation" title="Airgapped installation" href="../../install-software/installer/" coords="502,1053,666,1114" shape="rect" style="width: 19%;height: 3.4%;top: 55.4%;left: 55.5%;">
</map>
</div>

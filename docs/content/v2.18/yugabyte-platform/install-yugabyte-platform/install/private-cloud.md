---
title: Install YugabyteDB Anywhere on Private Cloud Overview
headerTitle: Overview
linkTitle: Overview
description: Installing YugabyteDB Anywhere on private cloud
image: /images/section_icons/deploy/enterprise.png
menu:
  v2.18_yugabyte-platform:
    identifier: install-3-private-cloud
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
    <a href="../private-cloud/" class="nav-link active">
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

The following diagram shows the YugabyteDB Anywhere installation process in a private cloud. Click elements of the chart to access detailed steps.

<div class="image-with-map">
<img src="/images/ee/flowchart/yb-install-private-cloud.png" usemap="#image-map">

<map name="image-map">
    <area alt="Pre reqs" title="Pre reqs" href="../../prerequisites/installer/" coords="323,255,572,412" shape="rect" style="top: 14%;height: 8.6%;left: 36%;width: 28%;">
    <area alt="Online installation" title="Online installation" href="../../install-software/installer/" coords="239,707,396,770" shape="rect" style="top: 40.7%;height: 3.5%;left: 25%;width: 20%;">
    <area alt="Airgapped installation" title="Airgapped installation" href="../../install-software/installer/" coords="512,709,663,767" shape="rect" style="top: 40.7%;height: 3.5%;left: 55%;width: 20%;">
    <area alt="Online installation - pre reqs" title="Online installation - pre reqs" href="../../install-software/installer/" coords="" shape="rect" style="top: 47%;height: 6%;left: 23%;width: 24%;">
    <area alt="Airgapped installation - pre reqs" title="Airgapped installation - pre reqs" href="../../install-software/installer/" coords="482,808,688,841" shape="rect" style="top: 47%;height: 10%;left: 53%;width: 24%;">
    <area alt="Prepare on premises nodes" title="Prepare on premises nodes" href="../../prepare-on-prem-nodes/" coords="307,1200,597,1250" shape="rect" style="top: 70.4%;height: 3.6%;left: 33%;width: 34%;">
</map>
</div>

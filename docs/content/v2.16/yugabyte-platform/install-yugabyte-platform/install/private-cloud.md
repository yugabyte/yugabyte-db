---
title: Install YugabyteDB Anywhere Overview
headerTitle: Overview
linkTitle: Overview
description: Installing YugabyteDB Anywhere on private cloud
image: /images/section_icons/deploy/enterprise.png
menu:
  v2.16_yugabyte-platform:
    identifier: install-3-private-cloud
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
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li >
    <a href="../private-cloud/" class="nav-link active">
      <i class="fa-solid fa-link-slash"></i>
      Private Cloud
    </a>
  </li>
</ul>

The following diagram depicts the YugabyteDB Anywhere installation process in a private cloud:

<div class="image-with-map">
<img src="/images/ee/flowchart/yb-install-private-cloud.png" usemap="#image-map">

<map name="image-map">
    <area alt="Pre reqs" title="Pre reqs" href="../../prerequisites/" coords="323,255,572,412" shape="rect" style="top: 14%;height: 8.6%;left: 36%;width: 28%;">
    <area alt="Online installation" title="Online installation" href="../../install-software/default/" coords="239,907,396,970" shape="rect" style="top: 48.7%;height: 3.5%;left: 25%;width: 20%;">
    <area alt="Airgapped installation" title="Airgapped installation" href="../../install-software/airgapped/" coords="512,909,663,967" shape="rect" style="top: 48.7%;height: 3.5%;left: 55%;width: 20%;">
    <area alt="Airgapped installation - pre reqs" title="Airgapped installation - pre reqs" href="../../install-software/airgapped/" coords="482,1008,688,1141" shape="rect" style="top: 54%;height: 8%;left: 53%;width: 24%;">
    <area alt="Online installation - pre reqs" title="Online installation - pre reqs" href="../../install-software/default/" coords="" shape="rect" style="top: 54%;height: 5%;left: 23%;width: 24%;">
    <area alt="Prepare on prem nodes" title="Prepare on prem nodes" href="../../prepare-on-prem-nodes/" coords="307,1371,597,1429" shape="rect" style="top: 73.4%;height: 3.6%;left: 33%;width: 34%;">
</map>
</div>

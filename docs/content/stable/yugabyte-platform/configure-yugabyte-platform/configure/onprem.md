---
title: Configure YugabyteDB Anywhere on premises
headerTitle: Overview
linkTitle: Overview
description: Configure YugabyteDB Anywhere on-premises
headcontent: Configure YugabyteDB Anywhere for on-premises
menu:
  stable_yugabyte-platform:
    identifier: configure-5-onprem
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
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="../onprem/" class="nav-link active">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>

</ul>

The following diagram depicts the configuration process for on-premises deployments.

<div class="image-with-map">
<img src="/images/ee/flowchart/yb-configure-onprem.png" usemap="#image-map">

<map name="image-map">
    <area alt="Create admin user" title="Create admin user" href="../../create-admin-user/" coords="296,260,607,320" shape="rect" style="top: 15.9%;height: 3.7%;left: 31%;width: 38%;">
    <area alt="On prem cloud provider" title="On prem cloud provider" href="../../set-up-cloud-provider/on-premises/" coords="247,369,653,424" shape="rect" style="top: 22.5%;height: 3.7%;left: 27%;width: 46%;">
    <area alt="configure on prem provider-1" title="configure on prem provider-1" href="../../set-up-cloud-provider/on-premises/#step-1-configure-the-on-premises-provider" coords="204,1230,425,1331" shape="rect" style="top: 75.3%;height: 6.1%;left: 22%;width: 25%;">
    <area alt="configure on prem provider-2" title="configure on prem provider-2" href="../../set-up-cloud-provider/on-premises/#step-1-configure-the-on-premises-provider" coords="474,1230,695,1328" shape="rect" style="top: 75.3%;height: 6.1%;left: 52.4%;width: 25%;">
</map>
</div>

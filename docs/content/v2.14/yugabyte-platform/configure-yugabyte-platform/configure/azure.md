---
title: Configure Azure
headerTitle: Overview
linkTitle: Overview
description: Configuring YugabyteDB Anywhere on Azure
headcontent: Configuring YugabyteDB Anywhere on Azure
menu:
  v2.14_yugabyte-platform:
    identifier: configure-3-azure
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
    <a href="../azure/" class="nav-link active">
      <i class="fa-brands fa-windows" aria-hidden="true"></i>
      Azure
    </a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
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

The following diagram depicts the configuration process for Azure.

<div class="image-with-map">
<img src="/images/ee/flowchart/yb-configure-azure.png" usemap="#image-map">

<map name="image-map">
    <area alt="Create admin user" title="Create admin user" href="../../create-admin-user/" coords="289,259,611,316" shape="rect" style="top:27%; left:31%; width:38%; height:6%;">
    <area alt="Configure Azure" title="Configure Azure" href="../../set-up-cloud-provider/azure/" coords="264,368,624,423" shape="rect" style="top: 38%; left: 29%; width: 42%; height: 7%;">
    <area alt="Azure provider - pre reqs" title="AWS provider - pre reqs" href="../../set-up-cloud-provider/azure/#prerequisites" coords="224,474,674,649" shape="rect" style=" width: 50%; height: 19%; top: 49.3%; left: 25%; ">
    <area alt="Azure provider - configure cloud provider" title="AWS provider - configure cloud provider" href="../../set-up-cloud-provider/azure/#configure-azure" coords="302,703,602,793" shape="rect" style=" width: 34%; height: 10%; top: 73%; left: 33%; ">
</map>
</div>

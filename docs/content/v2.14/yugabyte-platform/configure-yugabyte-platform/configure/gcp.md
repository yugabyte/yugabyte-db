---
title: Configure GCP
headerTitle: Overview
linkTitle: Overview
description: Configuring YugabyteDB Anywhere on Google Cloud Platform
headcontent: Configuring YugabyteDB Anywhere on Google Cloud Platform
menu:
  v2.14_yugabyte-platform:
    identifier: configure-2-gcp
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
    <a href="../gcp/" class="nav-link active">
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

The following diagram depicts the configuration process for GCP.

<div class="image-with-map">
<img src="/images/ee/flowchart/yb-configure-gcp.png" usemap="#image-map">

<map name="image-map">
    <area alt="Create admin user" title="Create admin user" href="../../create-admin-user/" coords="284,257,617,317" shape="rect" style=" width: 38%; height: 6%; top: 27%; left: 31%; ">
    <area alt="Configure GCP provider" title="Configure GCP provider" href="../../set-up-cloud-provider/gcp/" coords="249,369,647,423" shape="rect" style="width: 46%; height: 7%; top: 38%; left: 27%; ">
    <area alt="GCP provider pre reqs" title="GCP provider pre reqs" href="../../set-up-cloud-provider/gcp/#prerequisites" coords="223,476,675,653" shape="rect" style="width: 50%; height: 19%; top: 49%; left: 25%; ">
    <area alt="configure GCP" title="configure GCP" href="../../set-up-cloud-provider/gcp/#configure-gcp" coords="305,703,601,791" shape="rect" style="width: 34%; height: 10%; top: 73%; left: 33%; ">
</map>
</div>

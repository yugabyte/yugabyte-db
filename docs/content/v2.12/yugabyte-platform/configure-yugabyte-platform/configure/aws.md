---
title: Configure Yugabyte Platform
headerTitle: Overview
linkTitle: Overview
description: Configuring Yugabyte Platform on AWS
image: /images/section_icons/deploy/enterprise.png
headcontent: Configuring YugabyteDB Anywhere on AWS
menu:
  v2.12_yugabyte-platform:
    identifier: configure-1-aws
    parent: configure-yugabyte-platform
    weight: 5
type: docs
---

Select your cloud provider to see the steps for configuration of database nodes. Click on the elements to see detailed steps.

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../aws" class="nav-link active">
      <i class="fa-brands fa-aws"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../gcp" class="nav-link">
      <i class="fa-brands fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="../azure" class="nav-link">
      <i class="fa-brands fa-windows" aria-hidden="true"></i>
      Azure
    </a>
  </li>

  <li>
    <a href="../kubernetes" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="../onprem" class="nav-link">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>

</ul>

The following diagram depicts the configuration process for AWS.

<div class="image-with-map">
<img src="/images/ee/flowchart/yb-configure-aws.png" usemap="#image-map">

<map name="image-map">
    <area alt="Admin user" title="Admin user" href="../../create-admin-user/" coords="290,262,609,317" shape="rect" style=" width: 38%; height: 6%; top: 27%; left: 31%; ">
    <area alt="AWS provider" title="AWS provider" href="../../set-up-cloud-provider/aws/" coords="275,370,635,424" shape="rect" style=" width: 42%; height: 6%; top: 38.3%; left: 29%; ">
    <area alt="AWS provider - pre reqs" title="AWS provider - pre reqs" href="../../set-up-cloud-provider/aws/#prerequisites" coords="224,474,674,649" shape="rect" style=" width: 50%; height: 19%; top: 49.3%; left: 25%; ">
    <area alt="AWS provider - configure cloud provider" title="AWS provider - configure cloud provider" href="../../set-up-cloud-provider/aws/#configure-aws" coords="302,703,602,793" shape="rect" style=" width: 34%; height: 10%; top: 73%; left: 33%; ">
</map>
</div>

---
title: YugabyteDB Platform
headerTitle: YugabyteDB Platform
linkTitle: YugabyteDB Platform
description: YugabyteDB delivered as a private database-as-a-service for enterprises.
image: /images/section_icons/deploy/enterprise.png
headcontent: YugabyteDB delivered as a private database-as-a-service for enterprises.
menu:
  v2.12_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: overview-yp
    weight: 10
type: indexpage
---

### Overview

Yugabyte Platform is best fit for mission-critical deployments, such as production or pre-production testing. The Yugabyte Platform console is used in a highly-available mode and orchestrates and manages YugabyteDB universes, or clusters, on one or more regions (across public cloud and private on-premises data centers).

Yugabyte Platform is a containerized application that is installed and managed using <a href="https://www.replicated.com/" target="_blank">Replicated</a> for mission-critical environments (for example, production, performance, or failure mode testing). Replicated is a purpose-built tool for on-premises deployments and life cycle management of containerized applications. For environments that are not mission-critical, such as those needed for local development or simple functional testing, you can also use <a href="../../quick-start/install">YugabyteDB</a>.

Yugabyte Platform can be accessed using any desktop internet browser that has been supported by its maker in the past 24 months and that has a market share of at least 0.2%. In addition, Yugabyte Platform may be accessed by most mobile browsers, except Opera Mini.

Yugabyte Platform offers three levels of user accounts: Super Admin, Admin, and Read-only, with the latter having rather limited access to functionality. Unless otherwise specified, the Yugabyte Platform documentation describes the funtionality available to a Super Admin user.

## Getting Started with Yugabyte Platform

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="install-yugabyte-platform/install/public-cloud/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/install.png" aria-hidden="true" />
        <div class="title">Install</div>
      </div>
      <div class="body">
        Overview of installing Yugabyte Platform on any environment
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="configure-yugabyte-platform/configure/aws/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/create_cluster.png" aria-hidden="true" />
        <div class="title">Configure</div>
      </div>
      <div class="body">
        Configuring Yugabyte Platform to various providers
      </div>
    </a>
  </div>

</div>

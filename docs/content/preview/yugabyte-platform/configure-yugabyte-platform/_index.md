---
title: Configure YugabyteDB Anywhere
headerTitle: Configure YugabyteDB Anywhere
linkTitle: Configure
description: Configure YugabyteDB Anywhere.
image: /images/section_icons/deploy/manual-deployment.png
menu:
  preview_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: configure-yugabyte-platform
    weight: 610
type: indexpage
---

After YugabytDB Anywhere (YBA) has been installed, the next step is to create provider configurations. A provider configuration comprises all the parameters needed to deploy a YugabyteDB universe on the corresponding provider. This includes cloud credentials, regions and zones, networking details, and more.

When deploying a universe, YBA uses the provider configuration settings to create and provision the nodes that will make up the universe.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="configure/aws/">
      <div class="head">
        <img class="icon" src="/images/section_icons/deploy/manual-deployment.png" aria-hidden="true" />
        <div class="title">Overview</div>
      </div>
      <div class="body">
        Configuration process at a glance.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="supported-os-and-arch/">
      <div class="head">
        <img class="icon" src="/images/section_icons/deploy/manual-deployment.png" aria-hidden="true" />
        <div class="title">Node prerequisites</div>
      </div>
      <div class="body">
        Operating systems and architectures supported by YBA for deploying YugabyteDB universes.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="create-admin-user/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/edit_universe.png" aria-hidden="true" />
        <div class="title">Create admin user</div>
      </div>
      <div class="body">
        Admin user account registration and setup.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="set-up-cloud-provider/aws/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/edit_flags.png" aria-hidden="true" />
        <div class="title">Configure cloud providers</div>
      </div>
      <div class="body">
        Create AWS, GCP, Azure, Kubernetes, OpenShift, Tanzu, and On-premises providers.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="backup-target/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Configure backup targets</div>
      </div>
      <div class="body">
        Targets for scheduled backups of YugbyteDB universe data.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="set-up-alerts-health-check/">
      <div class="head">
        <img class="icon" src="/images/section_icons/deploy/manual-deployment.png" aria-hidden="true" />
        <div class="title">Configure alerts</div>
      </div>
      <div class="body">
        Health check and alerts for issues that may affect deployment.
      </div>
    </a>
  </div>

</div>

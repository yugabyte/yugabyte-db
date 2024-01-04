---
title: Upgrade YugabyteDB Anywhere
headerTitle: Upgrade YugabyteDB Anywhere
linkTitle: Upgrade
description: Upgrade YugabyteDB Anywhere.
image: /images/section_icons/quick_start/install.png
aliases:
menu:
  preview_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: upgrade
    weight: 700
type: indexpage
---

You can upgrade YugabyteDB Anywhere (YBA) using the following methods:

| Method | Using | Use If |
| :--- | :--- | :--- |
| [YBA&nbsp;Installer](./upgrade-yp-installer/) | yba-ctl CLI | Your installation already uses YBA Installer. |
| [Replicated](./upgrade-yp-replicated/) | Replicated Admin Console | Your installation already uses Replicated.<br>Before you can migrate from a Replicated installation, upgrade to v2.20.0 or later using Replicated. |
| [Kubernetes](./upgrade-yp-kubernetes/) | Helm chart | You're deploying in Kubernetes. |

If you are upgrading a YBA installation with high availability enabled, follow the instructions provided in [Upgrade instances](../administer-yugabyte-platform/high-availability/#upgrade-instances).

If you have upgraded YBA to version 2.12 or later and [xCluster replication](../../explore/multi-region-deployments/asynchronous-replication-ysql/) for your universe was set up via yb-admin instead of the UI, follow the instructions provided in [Synchronize replication after upgrade](upgrade-yp-xcluster-ybadmin/).

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="upgrade-yp-installer/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/install.png" aria-hidden="true" />
        <div class="title">Upgrade YugabyteDB Anywhere</div>
      </div>
      <div class="body">
        Upgrade your YugabyteDB Anywhere installation.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="upgrade-yp-xcluster-ybadmin/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/pitr.png" aria-hidden="true" />
        <div class="title">Synchronize replication after upgrade</div>
      </div>
      <div class="body">
        Synchronize xCluster replication after an upgrade for universes set up using yb-admin.
      </div>
    </a>
  </div>

</div>

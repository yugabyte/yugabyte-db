---
title: Connect to clusters in YugabyteDB Managed
linkTitle: Connect to clusters
description: Connect to clusters in YugabyteDB Managed.
headcontent: Connect using Cloud Shell, a client shell, and from applications
image: /images/section_icons/index/quick_start.png
aliases:
  - /preview/deploy/yugabyte-cloud/connect-to-clusters/
  - /preview/yugabyte-cloud/connect-to-clusters/
  - /preview/yugabyte-cloud/cloud-basics/connect-to-clusters/
menu:
  preview_yugabyte-cloud:
    parent: yugabytedb-managed
    identifier: cloud-connect
    weight: 40
type: indexpage
---

Connect to clusters in YugabyteDB Managed in the following ways:

| From | How |
| :--- | :--- |
| [Browser](connect-cloud-shell/) | Use Cloud Shell to connect to your database using any modern browser.<br>No need to set up an IP allow list, all you need is your database password.<br>Includes a built-in YSQL quick start guide. |
| [Desktop](connect-client-shell/) | Install the ysqlsh or ycqlsh client shells to connect to your database from your desktop.<br>YugabyteDB Managed also supports psql and [third-party tools](../../tools/) such as pgAdmin.<br>Requires your computer to be added to the cluster [IP allow list](../cloud-secure-clusters/add-connections/) and an [SSL connection](../cloud-secure-clusters/cloud-authentication/). |
| [Applications](connect-applications/) | Obtain the parameters needed to connect your application driver to your cluster database.<br>Requires the VPC or machine hosting the application to be added to the cluster IP allow list and an SSL connection. |

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="connect-cloud-shell/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/cloud_native.png" aria-hidden="true" />
        <div class="title">Cloud Shell</div>
      </div>
      <div class="body">
        Connect from your browser.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="connect-client-shell/">
      <div class="head">
        <img class="icon" src="/images/section_icons/index/develop.png" aria-hidden="true" />
        <div class="title">Client shell</div>
      </div>
      <div class="body">
        Connect from your desktop using a client shell.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="connect-applications/">
      <div class="head">
        <img class="icon" src="/images/section_icons/develop/real-world-apps.png" aria-hidden="true" />
        <div class="title">Applications</div>
      </div>
      <div class="body">
        Connect applications to your YugabyteDB Managed clusters.
      </div>
    </a>
  </div>

</div>

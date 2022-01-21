---
title: Connect to clusters in Yugabyte Cloud
linkTitle: Connect to clusters
description: Connect to clusters in Yugabyte Cloud.
headcontent: Connect to your cluster using the cloud shell, a client shell, and from applications.
image: /images/section_icons/index/quick_start.png
section: YUGABYTE CLOUD
aliases:
  - /latest/deploy/yugabyte-cloud/connect-to-clusters/
  - /latest/yugabyte-cloud/connect-to-clusters/
  - /latest/yugabyte-cloud/cloud-basics/connect-to-clusters/
menu:
  latest:
    identifier: cloud-connect
    weight: 40
isTocNested: true
showAsideToc: true
---

Connect to clusters in Yugabyte Cloud in the following ways:

- From a browser - Use cloud shell to connect to your database using any modern browser. No need to set up an IP allow list, all you need is your database password.
- From your desktop - Install the ysqlsh or ycqlsh client shells to connect to your database from your desktop. Your computer must be added to the cluster IP allow list and an SSL connection is required.
- Applications - Connect your applications to your cluster database.

Once you have connected to the database, you can add database users for other team members to access the database.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="connect-cloud-shell/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Cloud shell</div>
      </div>
      <div class="body">
        Connect from your browser.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="connect-client-shell/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
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
        Connect applications to your Yugabyte Cloud clusters.
      </div>
    </a>
  </div>

</div>

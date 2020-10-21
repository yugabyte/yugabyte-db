---
title: Secure universes
headerTitle: Secure universes
linkTitle: Secure universes
description: Use Yugabyte Platform to secure YugabyteDB universe data.
image: /images/section_icons/manage/backup.png
headcontent: Use Yugabyte Platform to secure YugabyteDB universes and data.
menu:
  latest:
    parent: yugabyte-platform
    identifier: secure-universes
weight: 645
---

Yugabyte Platform can create a YugabyteDB universe with many instances (VMs, pods, machines, etc., provided by IaaS), logically grouped together to form one logical distributed database. Each universe includes one or more clusters. A universe is comprised of one primary cluster and, optionally, one or more read replica clusters. All instances belonging to a cluster run on the same type of cloud provider instance type.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="create-kms-config/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Configure key management system (KMS)</div>
      </div>
      <div class="body">
        Use Yugabyte Platform to configure the key management system (KMS) for your universe data.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="back-up-universe-data/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Enable encryption at rest</div>
      </div>
      <div class="body">
        Use Yugabyte Platform to enable encryption at rest.
      </div>
    </a>
  </div>

</div>

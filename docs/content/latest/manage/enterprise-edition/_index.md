---
title: Enterprise Edition
linkTitle: Enterprise Edition
description: Enterprise Edition
image: /images/section_icons/manage/enterprise.png
headcontent: Manage YugaByte DB without any downtime using the Enterprise Edition's built-in orchestration and monitoring.
aliases:
  - /manage/enterprise-edition/
menu:
  latest:
    identifier: manage-enterprise-edition
    parent: manage
    weight: 707
---

YugaByte DB creates a `universe` with a bunch of instances (VMs, pods, machines etc provided by IaaS) logically grouped together to form one logical distributed database. Each such universe can be made up of one or more clusters. These are comprised of one `Primary` cluster and zero or more `Read Replica` clusters. All instances belonging to a cluster run on the same type of cloud provider instance type.

<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="create-universe-multi-zone/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/create_universe.png" aria-hidden="true" />
        <div class="title">Create Universe - Multi-Zone</div>
      </div>
      <div class="body">
        Create YugaByte DB universes in one region across multiple zones using YugaByte Admin Console's intent-driven orchestration.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="create-universe-multi-region/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/create_universe.png" aria-hidden="true" />
        <div class="title">Create Universe - Multi-Region</div>
      </div>
      <div class="body">
        Create YugaByte DB universes in multiple regions using YugaByte Admin Console's intent-driven orchestration.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="edit-universe/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/edit_universe.png" aria-hidden="true" />   
        <div class="title">Edit Universe</div>
      </div>
      <div class="body">
        Expand, shrink and reconfigure universes without any downtime.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="edit-config/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/edit_flags.png" aria-hidden="true" />    
        <div class="title">Edit Config Flags</div>
      </div>
      <div class="body">
        Change the config flags for universes in a rolling manner without any application impact.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="cluster-health/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/diagnostics.png" aria-hidden="true" />
        <div class="title">Health Checks and Alerts</div>
      </div>
      <div class="body">
        Setup automatic cluster health checking and error reporting.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="read-replicas/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/create_universe.png" aria-hidden="true" />
        <div class="title">Read Replicas</div>
      </div>
      <div class="body">
        Create YugaByte DB universes with primary and read replica clusters in a hybrid cloud deployment.
      </div>
    </a>
  </div>
  
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="backup-restore/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise.png" aria-hidden="true" />
        <div class="title">Backup Restore</div>
      </div>
      <div class="body">
        Backup and Restore tables using the YugaByte Admin Console.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="upgrade-universe/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/upgrade_universe.png" aria-hidden="true" />   
        <div class="title">Upgrade Universe</div>
      </div>
      <div class="body">
        Upgrade universes in a rolling manner without any application impact.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="node-actions/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/edit_universe.png" aria-hidden="true" />
        <div class="title">Node Status and Actions</div>
      </div>
      <div class="body">
        Node status and actions to transition them for node maintenance.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="delete-universe/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/delete_universe.png" aria-hidden="true" /> 
        <div class="title">Delete Universe</div>
      </div>
      <div class="body">
        Delete unwanted universes to free up infrastructure capacity.
      </div>
    </a>
  </div>
</div>

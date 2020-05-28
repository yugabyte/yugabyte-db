---
title: Yugabyte Platform
headerTitle: Yugabyte Platform
linkTitle: Yugabyte Platform
description: Manage YugabyteDB without any downtime using the Yugabyte Platform's built-in orchestration and monitoring.
image: /images/section_icons/manage/enterprise.png
headcontent: Manage YugabyteDB without any downtime using the Yugabyte Platform's built-in orchestration and monitoring.
aliases:
  - /manage/enterprise-edition/
menu:
  latest:
    identifier: yugabyte-platform
    parent: manage
    weight: 707
---

YugabyteDB creates a universe with many instances (VMs, pods, machines, etc., provided by IaaS) logically grouped together to form one logical distributed database. Each such universe can be made up of one or more clusters. These are comprised of one primary cluster and zero or more read replica clusters. All instances belonging to a cluster run on the same type of cloud provider instance type.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="create-universe-multi-zone/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/create_universe.png" aria-hidden="true" />
        <div class="title">Create a multi-zone universe</div>
      </div>
      <div class="body">
        Use Yugabyte Platform to create a universe across multiple zones.
      </div>
    </a>

  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="create-universe-multi-region/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/create_universe.png" aria-hidden="true" />
        <div class="title">Create a multi-region universe</div>
      </div>
      <div class="body">
        Use Yugabyte Platform to create a universe across multiple regions.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="edit-universe/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/edit_universe.png" aria-hidden="true" />   
        <div class="title">Edit a universe</div>
      </div>
      <div class="body">
        Expand, shrink, and reconfigure universes without downtime.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="edit-config/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/edit_flags.png" aria-hidden="true" />    
        <div class="title">Edit configuration flags</div>
      </div>
      <div class="body">
        Change universe configuration flags in a rolling manner without any application impact.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="instance-tags/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/edit_flags.png" aria-hidden="true" />    
        <div class="title">Create and edit instance tags</div>
      </div>
      <div class="body">
        Create and edit instance tags.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="cluster-health/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/diagnostics.png" aria-hidden="true" />
        <div class="title">Configure health checks and alerts</div>
      </div>
      <div class="body">
        Configure automatic cluster health checking and error reporting.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="read-replicas/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/create_universe.png" aria-hidden="true" />
        <div class="title">Create a read replica cluster</div>
      </div>
      <div class="body">
        Create YugabyteDB universes with primary and read replica clusters in a hybrid cloud deployment.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="backup-restore/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise.png" aria-hidden="true" />
        <div class="title">Back up and restore data</div>
      </div>
      <div class="body">
        Back up and restore data using the YugabyteDB Admin Console.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="back-up-metadata/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise.png" aria-hidden="true" />
        <div class="title">Back up and restore Yugabyte Platform</div>
      </div>
      <div class="body">
        Back up and restore Yugabyte Platform for disaster recovery.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="schedule-backup/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise.png" aria-hidden="true" />
        <div class="title">Schedule a backup</div>
      </div>
      <div class="body">
        Schedule a full universe backup or backups of selected tables.
      </div>
    </a>
  </div>


  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="upgrade-universe/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/upgrade_universe.png" aria-hidden="true" />   
        <div class="title">Upgrade a universe</div>
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
        <div class="title">Node status and actions</div>
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
        <div class="title">Delete a universe</div>
      </div>
      <div class="body">
        Delete an unnecessary universe to free up infrastructure capacity.
      </div>
    </a>
  </div>

</div>

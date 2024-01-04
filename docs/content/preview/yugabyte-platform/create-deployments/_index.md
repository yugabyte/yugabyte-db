---
title: Create YugabyteDB universe deployments
headerTitle: Create universes
linkTitle: Create universes
description: Create YugabyteDB universe deployments.
image: /images/section_icons/index/deploy.png
menu:
  preview_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: create-deployments
    weight: 630
type: indexpage
---

YugabyteDB Anywhere can create a YugabyteDB universe with many instances (virtual machines, pods, and so on, provided by IaaS), logically grouped together to form one distributed database.

A universe includes one primary cluster and, optionally, one read replica cluster. All instances belonging to a cluster run on the same type of cloud provider instance.

<div class="row">

<!--
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="create-universe-single-zone/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/diagnostics.png" aria-hidden="true" />
        <div class="title">Create a single-zone universe</div>
      </div>
      <div class="body">
        Use YugabyteDB Anywhere to create a single-zone universe.
      </div>
    </a>
  </div>
-->

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="create-universe-multi-zone/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/create_cluster.png" aria-hidden="true" />
        <div class="title">Create a multi-zone universe</div>
      </div>
      <div class="body">
        Use YugabyteDB Anywhere to deploy a multi-zone universe.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="create-universe-multi-region/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/planet_scale.png" aria-hidden="true" />
        <div class="title">Create a multi-region universe</div>
      </div>
      <div class="body">
        Use YugabyteDB Anywhere to deploy a multi-region universe.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="create-universe-multi-cloud/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/planet_scale.png" aria-hidden="true" />
        <div class="title">Create a multi-cloud universe</div>
      </div>
      <div class="body">
        Use YugabyteDB Anywhere to deploy a multi-cloud universe.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="read-replicas/">
      <div class="head">
        <img class="icon" src="/images/deploy/cdc/deploy.png" aria-hidden="true" />
        <div class="title">Create a read-replica cluster</div>
      </div>
      <div class="body">
        Use YugabyteDB Anywhere to create a read-replica cluster for a universe.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="async-replication-platform/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/planet_scale.png" aria-hidden="true" />
        <div class="title">Enable xCluster replication </div>
      </div>
      <div class="body">
        Enable unidirectional and bidirectional replication between two data sources
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="dedicated-master">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/planet_scale.png" aria-hidden="true" />
        <div class="title">Place YB-Masters on dedicated nodes</div>
      </div>
      <div class="body">
        Create a universe with YB-Master and YB-TServer processes on dedicated nodes.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="connect-to-universe">
      <div class="head">
        <img class="icon" src="/images/section_icons/index/develop.png" aria-hidden="true" />
        <div class="title">Connect to a universe</div>
      </div>
      <div class="body">
        Connect to your universe using a client shell.
      </div>
    </a>
  </div>

</div>

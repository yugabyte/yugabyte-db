---
title: YugabyteDB configuration reference
headerTitle: Configuration
linkTitle: Configuration
description: YugabyteDB configuration reference for core database services, including yb-tserver, yb-master, and yugabyted.
headcontent: Configure core database services
image: /images/section_icons/deploy/enterprise/administer.png
menu:
  stable:
    identifier: configuration
    parent: reference
    weight: 2600
type: indexpage
---
YugabyteDB uses a two-server architecture, with YB-TServers managing the data and YB-Masters managing the metadata. You can use the yb-tserver and yb-master binaries to start and configure the servers.

For simplified deployment, use the yugabyted utility. yugabyted acts as a parent server across the YB-TServer and YB-Masters servers.

<div class="row">

   <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="yb-tserver/">
      <div class="head">
        <img class="icon" src="/images/section_icons/reference/configuration/yb-tserver.png" aria-hidden="true" />
        <div class="title">yb-tserver reference</div>
      </div>
      <div class="body">
        Configure YB-TServer servers to store and manage data for client applications.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="yb-master/">
      <div class="head">
        <img class="icon" src="/images/section_icons/reference/configuration/yb-master.png" aria-hidden="true" />
        <div class="title">yb-master reference</div>
      </div>
      <div class="body">
        Configure YB-Master servers to manage cluster metadata, tablets, and coordination of cluster-wide operations.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="yugabyted/">
      <div class="head">
        <img class="icon" src="/images/section_icons/deploy/manual-deployment.png" aria-hidden="true" />
        <div class="title">yugabyted reference</div>
      </div>
      <div class="body">
        Use the yugabyted utility to launch and manage universes.
      </div>
    </a>
  </div>

</div>

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="operating-systems/">
      <div class="head">
        <img class="icon" src="/images/section_icons/deploy/enterprise/administer.png" aria-hidden="true" />
        <div class="title">Supported operating systems</div>
      </div>
      <div class="body">
        Supported operating systems for deploying YugabyteDB and YugabyteDB Anywhere.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="default-ports/">
      <div class="head">
        <img class="icon" src="/images/section_icons/deploy/enterprise/administer.png" aria-hidden="true" />
        <div class="title">Default ports</div>
      </div>
      <div class="body">
        Default ports for APIs, RPCs, and admin web servers.
      </div>
    </a>
  </div>

</div>

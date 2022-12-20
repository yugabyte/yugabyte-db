---
title: YugabyteDB Anywhere
headerTitle: YugabyteDB Anywhere
linkTitle: YugabyteDB Anywhere
description: YugabyteDB delivered as a private database-as-a-service for enterprises.
aliases:
  - /preview/yugabyte-platform/overview/
menu:
  preview_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: overview-yp
    weight: 10
type: indexpage
breadcrumbDisable: true
body_class: yb-page-style
resourcesIntro: Quick Links
resources:
  - title: What's new
    url: /preview/releases/release-notes/v2.15/
  - title: FAQ
    url: /preview/faq/yugabyte-platform/
  - title: Free trial
    url: https://www.yugabyte.com/anywhere/
---

YugabyteDB Anywhere is best fit for mission-critical deployments, such as production or pre-production testing. The YugabyteDB Anywhere UI is used in a highly-available mode, allowing you to create and manage YugabyteDB universes, or clusters, on one or more regions across public cloud and private on-premises data centers.

YugabyteDB Anywhere is a containerized application that you can install using [Replicated](https://www.replicated.com/) for production, performance, or failure mode testing. For local development or functional testing you can also use [YugabyteDB](../quick-start/).

You can access YugabyteDB Anywhere via an Internet browser that has been supported by its maker in the past 24 months and that has a market share of at least 0.2%. In addition, you can access YugabyteDB Anywhere via most mobile browsers, except Opera Mini.

YugabyteDB Anywhere offers three levels of user accounts: Super Admin, Admin, and Read-only, with the latter having rather limited access to functionality. Unless otherwise specified, the YugabyteDB Anywhere documentation describes the functionality available to a Super Admin user.

<div class="row cloud-laptop">
  <div class="col-12 col-md-12 col-lg-6">
    <div class="border two-side">
      <div class="body">
        <div class="box-top">
          <span>Install</span>
        </div>
        <div class="body-content">Install YugabyteDB Anywhere on any environment, including Kubernetes, public cloud, or private cloud.</div>
        <a class="text-link" href="install-yugabyte-platform/" title="Install">Learn more</a>
      </div>
      <div class="image">
        <img class="icon" src="/images/homepage/yugabyte-in-cloud.png" title="Yugabyte cloud" aria-hidden="true">
      </div>
    </div>
  </div>
  <div class="col-12 col-md-12 col-lg-6">
    <div class="border two-side">
      <div class="body">
        <div class="box-top">
          <span>Configure</span>
        </div>
        <div class="body-content">Confiure YugabyteDB Anywhere for various cloud providers.</div>
        <a class="text-link" href="configure-yugabyte-platform/" title="Configure">Learn more</a>
      </div>
      <div class="image">
        <img class="icon" src="/images/homepage/locally-laptop.png" title="Locally Laptop" aria-hidden="true">
      </div>
    </div>
  </div>
</div>

<div class="three-box-row">
  <div class="row">
    <h2 class="col-12">Use YugabyteDB Anywhere</h2>
    <div class="col-12 col-md-6 col-lg-4">
      <a href="create-deployments/" title="Deploy">
        <div class="box border">
          <div class="other-content">
            <div class="heading">Deploy</div>
            <div class="detail-copy">Deploy multi-region, multi-zone, and multi-cloud universes.</div>
          </div>
        </div>
      </a>
    </div>
    <div class="col-12 col-md-6 col-lg-4">
      <a href="manage-deployments/" title="Manage">
        <div class="box border">
          <div class="other-content">
            <div class="heading">Manage</div>
            <div class="detail-copy">Modify universes and their nodes, upgrade YugabyteDB software.</div>
          </div>
        </div>
      </a>
    </div>
    <div class="col-12 col-md-6 col-lg-4">
      <a href="back-up-restore-universes/" title="Back up">
        <div class="box border">
          <div class="other-content">
            <div class="heading">Back up</div>
            <div class="detail-copy">Configure storage, back up and restore universe data.</div>
          </div>
        </div>
      </a>
    </div>
    <!--
    <div class="col-12 col-md-6 col-lg-4">
      <a href="security/" title="Secure">
        <div class="box border">
          <div class="other-content">
            <div class="heading">Secure</div>
            <div class="detail-copy">Configure authentication and authorization, use key management, enable encryption, provide network security.</div>
          </div>
        </div>
      </a>
    </div>
    <div class="col-12 col-md-6 col-lg-4">
      <a href="alerts-monitoring/" title="Monitor">
        <div class="box border">
          <div class="other-content">
            <div class="heading">Monitor</div>
            <div class="detail-copy">Configure alerts to monitor YugabyteDB universe data.</div>
          </div>
        </div>
      </a>
    </div>
    <div class="col-12 col-md-6 col-lg-4">
      <a href="troubleshoot/" title="Troubleshoot">
        <div class="box border">
          <div class="other-content">
            <div class="heading">Troubleshoot</div>
            <div class="detail-copy">Diagnose and troubleshoot issues that arise from YugabyteDB universes and YugabyteDB Anywhere.</div>
          </div>
        </div>
      </a>
    </div>
    <div class="col-12 col-md-6 col-lg-4">
      <a href="administer-yugabyte-platform/" title="Administer">
        <div class="box border">
          <div class="other-content">
            <div class="heading">Administer</div>
            <div class="detail-copy">Back up and restore the server, as well as configure authentication for the server login.</div>
          </div>
        </div>
      </a>
    </div>
    <div class="col-12 col-md-6 col-lg-4">
      <a href="upgrade/" title="Upgrade">
        <div class="box border">
          <div class="other-content">
            <div class="heading">Upgrade</div>
            <div class="detail-copy">Upgrade to a newer version of YugabyteDB Anywhere.</div>
          </div>
        </div>
      </a>
    </div>
-->
  </div>
</div>

<div class="three-box-row">
  <div class="row">
    <h2 class="col-12">Additional resources</h2>
    <div class="col-12 col-md-6 col-lg-4">
      <div class="box border two-side-">
        <div class="other-content">
          <div class="heading">Build applications</div>
          <div class="detail-copy">Start coding in your favorite programming language using examples.</div>
          <a class="text-link" href="../develop/build-apps/" title="Get started">Get started</a>
        </div>
      </div>
    </div>
    <div class="col-12 col-md-6 col-lg-4">
      <div class="box border two-side-">
        <div class="other-content">
          <div class="heading">Yugabyte University</div>
          <div class="detail-copy">Take free courses and workshops to learn YugabyteDB, YSQL, and YCQL.</div>
          <ul>
            <li><a class="text-link" target="_blank" href="https://university.yugabyte.com/collections/builder-workshop" title="Course 2" target="_blank" rel="noopener">Developer workshops</a></li>
            <li><a class="text-link" target="_blank" href="https://university.yugabyte.com/courses/ysql-exercises-simple-queries" title="Course 3" target="_blank" rel="noopener">YSQL exercises</a></li>
          </ul>
        </div>
      </div>
    </div>
    <div class="col-12 col-md-6 col-lg-4">
      <div class="box border two-side-">
        <div class="other-content">
          <div class="heading">Support</div>
          <div class="detail-copy">Ask questions, request assistance from our team, participate in our journey.</div>
          <ul>
            <li><a class="text-link" target="_blank" href="https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360001955891" title="Contact Support">Contact Support</a></li>
            <li><a class="text-link" target="_blank" href="https://communityinviter.com/apps/yugabyte-db/register" title="Join our community">Join our community</a></li>
          </ul>
        </div>
      </div>
    </div>
  </div>
</div>

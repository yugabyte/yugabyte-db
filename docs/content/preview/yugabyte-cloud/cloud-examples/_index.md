---
title: Example applications
linkTitle: Example applications
description: Example applications for YugabyteDB Managed.
headcontent: Example applications for YugabyteDB Managed
image: /images/section_icons/index/develop.png
menu:
  preview_yugabyte-cloud:
    parent: yugabytedb-managed
    identifier: cloud-examples
    weight: 800
type: indexpage
---

The sample applications in this section provide advanced examples of connecting Spring, GraphQL, and YCQL Java applications to a YugabyteDB Managed cluster.

To get started building applications for YugabyteDB Managed, refer to [Build an application](../../tutorials/build-apps/).

Applications connect to and interact with YugabyteDB using API client libraries (also known as client drivers). Before you can connect an application, you need to install the correct driver. Because the YugabyteDB YSQL API is PostgreSQL-compatible, and the YCQL API has roots in the Apache Cassandra CQL, YugabyteDB supports many third-party drivers. For information on available drivers, refer to [Drivers](../../reference/drivers/).

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="connect-application/">
      <div class="head">
        <img class="icon" src="/images/section_icons/develop/learn.png" aria-hidden="true" />
        <div class="title">Connect a Spring Data YugabyteDB application</div>
      </div>
      <div class="body">
        Connect a Spring application implemented with Spring Data YugabyteDB.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="connect-ycql-application/">
      <div class="head">
        <img class="icon" src="/images/section_icons/develop/learn.png" aria-hidden="true" />
        <div class="title">Connect a YCQL application</div>
      </div>
      <div class="body">
        Connect a YCQL Java application.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="hasura-cloud/">
      <div class="head">
        <img class="icon" src="/images/section_icons/develop/real-world-apps.png" aria-hidden="true" />
        <div class="title">Connect to Hasura Cloud</div>
      </div>
      <div class="body">
        Connect a YugabyteDB Managed cluster to a Hasura Cloud project.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="hasura-sample-app/">
      <div class="head">
        <img class="icon" src="/images/section_icons/develop/real-world-apps.png" aria-hidden="true" />
        <div class="title">Deploy a GraphQL application</div>
      </div>
      <div class="body">
        Deploy a real-time polling application connected to YugabyteDB Managed on Hasura Cloud.
      </div>
    </a>
  </div>

</div>

---
title: Build applications
headerTitle: Build an application
linkTitle: Build an application
description: Build an application using your favorite programming language.
headcontent: Use your favorite programming language to build an application that uses YSQL or YCQL APIs.
image: /images/section_icons/develop/api-icon.png
menu:
  latest:
    identifier: cloud-build-apps
    parent: cloud-quickstart
    weight: 500
---

Applications connect to and interact with YugabyteDB using API client libraries, also called client drivers. The tutorials in this section show how to connect applications to Yugabyte Cloud clusters using available drivers and ORMs. <!--Because the YugabyteDB YSQL API is PostgreSQL-compatible and the YCQL API has roots in the Apache Cassandra CQL, many of the tutorials use third-party drivers.-->

To run these applications with Yugabyte Cloud, you will need the following:

- A cluster deployed in Yugabyte Cloud. To get started, use the [Quick start](../).
- The cluster CA certificate installed on your machine. Refer to [Download your cluster certificate](../../cloud-secure-clusters/cloud-authentication/#download-your-cluster-certificate).
- Your computer added to the cluster IP allow list. Refer to [Assign IP Allow Lists](../../cloud-secure-clusters/add-connections/).

For details about supported API client drivers (by programming language), see <a href="../../../reference/drivers">Drivers</a>.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="cloud-apps-java/cloud-ysql-yb-jdbc/">
    <div class="head">
      <div class="icon">
        <i class="icon-java"></i>
      </div>
      <div class="title">Java</div>
    </div>
    <div class="body">
      Java application that connects to a YugabyteDB cluster using the Yugabyte JDBC driver.
    </div>
  </a>
  </div>

</div>

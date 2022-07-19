---
title: Build applications
headerTitle: Build an application
linkTitle: Build an application
description: Build an application using your favorite programming language.
headcontent: Use your favorite programming language to build an application that uses YSQL or YCQL APIs.
image: /images/section_icons/develop/api-icon.png
aliases:
  - /preview/yugabyte-cloud/cloud-develop/
menu:
  preview_yugabyte-cloud:
    identifier: cloud-build-apps
    parent: cloud-quickstart
    weight: 500
type: indexpage
---

Applications connect to and interact with YugabyteDB using API client libraries (also known as client drivers). The tutorials in this section show how to connect applications to YugabyteDB Managed clusters using available drivers and ORMs.

{{< youtube id="uC0sJ_XPhCw" title="Create a sample application for YugabyteDB Managed" >}}

Because the YugabyteDB YSQL API is PostgreSQL-compatible, and the YCQL API has roots in the Apache Cassandra CQL, YugabyteDB supports many third-party drivers. For details about supported client drivers (by programming language), see [Drivers and ORMs](../../../drivers-orms/).

{{< note title="Note" >}}

To take advantage of smart driver load balancing features when connecting to clusters in YugabyteDB Managed, applications using smart drivers must be deployed in a VPC that has been peered with the cluster VPC. For information on VPC networking in YugabyteDB Managed, refer to [VPC network](../../cloud-basics/cloud-vpcs/).

For applications that access the cluster from a non-peered network, use the upstream PostgreSQL driver instead; in this case, the cluster performs the load balancing. Applications that use smart drivers from non-peered networks fall back to the upstream driver behaviour automatically.

{{< /note >}}

For more advanced applications, including Spring and GraphQL examples, refer to [Example applications](../../cloud-examples/).

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="cloud-add-ip/">
    <div class="head">
        <img class="icon" src="/images/section_icons/deploy/checklist.png" aria-hidden="true" />
      <div class="title">Before you begin</div>
    </div>
    <div class="body">
      Download your cluster CA certificate and add your computer to the IP allow list.
    </div>
  </a>
  </div>
</div>

## Choose your language

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="cloud-ysql-yb-jdbc/">
    <div class="head">
      <div class="icon">
        <i class="fa-brands fa-java"></i>
      </div>
      <div class="title">Java</div>
    </div>
    <div class="body">
      Java application that connects to a YugabyteDB cluster using the Yugabyte JDBC driver.
    </div>
  </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="cloud-ysql-go/">
    <div class="head">
      <div class="icon">
        <i class="fa-brands fa-golang"></i>
      </div>
      <div class="title">Go</div>
    </div>
    <div class="body">
      Go application that connects to a YugabyteDB cluster using the Go PostgreSQL driver.
    </div>
  </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="cloud-ysql-python/">
    <div class="head">
      <div class="icon">
        <i class="fa-brands fa-python"></i>
      </div>
      <div class="title">Python</div>
    </div>
    <div class="body">
      Python application that connects to a YugabyteDB cluster using the Psycopg PostgreSQL database adapter.
    </div>
  </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="cloud-ysql-node/">
    <div class="head">
      <div class="icon">
        <i class="fa-brands fa-node-js"></i>
      </div>
      <div class="title">NodeJS</div>
    </div>
    <div class="body">
      NodeJS application that connects to a YugabyteDB cluster using the node-postgres module.
    </div>
  </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="cloud-ysql-c/">
    <div class="head">
      <div class="icon">
        <i class="fa-brands fa-c"></i>
      </div>
      <div class="title">C</div>
    </div>
    <div class="body">
      C application that connects to a YugabyteDB cluster using the libpq driver.
    </div>
  </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="cloud-ysql-cpp/">
    <div class="head">
      <div class="icon">
        <i class="icon-cplusplus"></i>
      </div>
      <div class="title">C++</div>
    </div>
    <div class="body">
      C++ application that connects to a YugabyteDB cluster using the libpqxx driver.
    </div>
  </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="cloud-ysql-csharp/">
    <div class="head">
      <div class="icon">
        <i class="icon-csharp"></i>
      </div>
      <div class="title">C#</div>
    </div>
    <div class="body">
      C# application that connects to a YugabyteDB cluster using the Npgsql driver.
    </div>
  </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="cloud-ysql-ruby/">
    <div class="head">
      <div class="icon">
        <i class="icon-ruby"></i>
      </div>
      <div class="title">Ruby</div>
    </div>
    <div class="body">
      Ruby application that connects to a YugabyteDB cluster using the Ruby pg driver.
    </div>
  </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="cloud-ysql-rust/">
    <div class="head">
      <div class="icon">
        <i class="fa-brands fa-rust"></i>
      </div>
      <div class="title">Rust</div>
    </div>
    <div class="body">
      Rust application that connects to a YugabyteDB cluster using the Rust-Postgres driver.
    </div>
  </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="cloud-ysql-php/">
    <div class="head">
      <div class="icon">
        <i class="fa-brands fa-php"></i>
      </div>
      <div class="title">PHP</div>
    </div>
    <div class="body">
      PHP application that connects to a YugabyteDB cluster using the php-pgsql driver.
    </div>
  </a>
  </div>

</div>

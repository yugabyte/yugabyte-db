---
title: Build applications
headerTitle: Build an application
linkTitle: Build an application
description: Build an application using your favorite programming language.
headContent: Use your favorite programming language to build an application that uses YSQL or YCQL APIs.
image: /images/section_icons/develop/api-icon.png
aliases:
  - /develop/client-drivers/
  - /preview/develop/client-drivers/
  - /preview/quick-start/build-apps/
menu:
  preview:
    identifier: build-apps
    parent: develop
    weight: 150
type: indexpage
showrightnav: true
---

Applications connect to and interact with YugabyteDB using API client libraries, also called client drivers. Because the YugabyteDB YSQL API is PostgreSQL-compatible and the YCQL API has roots in the Apache Cassandra CQL, you can use a variety of third-party drivers to build applications for YugabyteDB. The tutorials in this section show how to connect applications to YugabyteDB clusters using available drivers and ORMs.

For details about supported API client drivers (by programming language), see <a href="../../drivers-orms/">Drivers and ORMs</a>.\
\
{{< youtube id="uC0sJ_XPhCw" title="Create a sample application for YugabyteDB Managed" >}}

To run these examples, you need a cluster deployed in YugabyteDB Managed or locally. See the [Quick Start](../../quick-start-yugabytedb-managed/).

For YugabyteDB Managed, you also need to do the following:

- **Download the cluster CA certificate**. YugabyteDB Managed uses TLS 1.2 for communicating with clusters, and digital certificates to verify the identity of clusters. The [cluster CA certificate](../../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/) is used to verify the identity of the cluster when you connect to it from an application or client.
- **Add your computer to the cluster IP allow list**. Access to YugabyteDB Managed clusters is limited to IP addresses that you explicitly allow using IP allow lists. To enable applications to connect to your cluster, you need to add your computer's IP address to the cluster [IP allow list](../../yugabyte-cloud/cloud-secure-clusters/add-connections/).

For instructions, see [Before you begin](before-you-begin/).

## Example applications

The following tutorials show how to connect applications to YugabyteDB using common drivers:

- [Java](simple-ysql-yb-jdbc/)
- [Go](simple-ysql-go/)
- [Python](simple-ysql-python/)
- [NodeJS](simple-ysql-node/)
- [C](simple-ysql-c/)
- [C++](simple-ysql-cpp/)
- [C#](simple-ysql-csharp/)
- [Ruby](simple-ysql-ruby/)
- [Rust](simple-ysql-rust/)
- [PHP](simple-ysql-php/)

## Additional driver examples

The following tutorials provide additional driver examples.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="additional-examples/java/ysql-yb-jdbc/">
      <div class="head">
        <div class="icon">
          <i class="fa-brands fa-java"></i>
        </div>
        <div class="title">Java</div>
      </div>
      <div class="body">
        Build applications using Java.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="additional-examples/nodejs/ysql-pg/">
      <div class="head">
        <div class="icon">
          <i class="fa-brands fa-node-js"></i>
        </div>
        <div class="title">NodeJS</div>
      </div>
      <div class="body">
        Build applications using NodeJS.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="additional-examples/go/ysql-yb-pgx/">
      <div class="head">
        <div class="icon">
          <i class="fa-brands fa-golang"></i>
        </div>
        <div class="title">Go</div>
      </div>
      <div class="body">
        Build applications using Go.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="additional-examples/python/ysql-psycopg2/">
      <div class="head">
        <div class="icon">
          <i class="fa-brands fa-python"></i>
        </div>
        <div class="title">Python</div>
      </div>
      <div class="body">
        Build applications using Python.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="additional-examples/ruby/ysql-pg/">
      <div class="head">
        <div class="icon">
          <i class="icon-ruby"></i>
        </div>
        <div class="title">Ruby</div>
      </div>
      <div class="body">
        Build applications using Ruby.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="additional-examples/csharp/ysql/">
      <div class="head">
        <div class="icon">
          <i class="icon-csharp"></i>
        </div>
        <div class="title">C#</div>
      </div>
      <div class="body">
        Build applications using C#.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="additional-examples/php/ysql/">
      <div class="head">
        <div class="icon">
          <i class="fa-brands fa-php"></i>
        </div>
        <div class="title">PHP</div>
      </div>
      <div class="body">
        Build applications using PHP.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="additional-examples/cpp/ysql/">
      <div class="head">
        <div class="icon">
          <i class="icon-cplusplus"></i>
        </div>
        <div class="title">C++</div>
      </div>
      <div class="body">
        Build applications using C++.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="additional-examples/c/ysql/">
      <div class="head">
        <div class="icon">
          <i class="icon-c"></i>
        </div>
        <div class="title">C</div>
      </div>
      <div class="body">
        Build applications using C.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="additional-examples/scala/ycql/">
      <div class="head">
        <div class="icon">
          <i class="icon-scala"></i>
        </div>
        <div class="title">Scala</div>
      </div>
      <div class="body">
        Build applications using Scala.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="additional-examples/rust/ysql-diesel/">
      <div class="head">
        <div class="icon">
          <i class="icon-scala"></i>
        </div>
        <div class="title">Rust</div>
      </div>
      <div class="body">
        Build applications using Rust.
      </div>
    </a>
  </div>
</div>

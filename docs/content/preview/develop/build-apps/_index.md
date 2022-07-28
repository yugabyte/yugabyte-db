---
title: Build applications
headerTitle: Build an application
linkTitle: Build an application
description: Build an application using your favorite programming language.
headcontent: Use your favorite programming language to build an application that uses YSQL or YCQL APIs.
image: /images/section_icons/develop/api-icon.png
aliases:
  - /develop/client-drivers/
  - /preview/develop/client-drivers/
  - /preview/quick-start/build-apps/
  - /preview/yugabyte-cloud/cloud-quickstart/cloud-build-apps/
menu:
  preview:
    identifier: build-apps
    parent: develop
    weight: 150
type: indexpage
---

Applications connect to and interact with YugabyteDB using API client libraries (also known as client drivers). Because the YugabyteDB YSQL API is PostgreSQL-compatible, and the YCQL API has roots in the Apache Cassandra CQL, YugabyteDB supports many third-party drivers.

The tutorials in this section show how to connect applications to YugabyteDB using available [Drivers and ORMs](../../drivers-orms/).

The tutorials assume you have deployed a YugabyteDB cluster in YugabyteDB Managed or locally. Refer to [Quick start](../../quick-start-yugabytedb-managed/).<br><br>

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="cloud-add-ip/">
    <div class="head">
        <img class="icon" src="/images/section_icons/deploy/checklist.png" aria-hidden="true" />
      <div class="title">Before you begin</div>
    </div>
    <div class="body">
      If your cluster is in YugabyteDB Managed, start here.
    </div>
  </a>
  </div>
</div>

## Choose your language

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="java/cloud-ysql-yb-jdbc/">
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
    <a class="section-link icon-offset" href="go/cloud-ysql-go/">
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
    <a class="section-link icon-offset" href="python/cloud-ysql-python/">
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
    <a class="section-link icon-offset" href="nodejs/cloud-ysql-node/">
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
    <a class="section-link icon-offset" href="c/cloud-ysql-c/">
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
    <a class="section-link icon-offset" href="cpp/cloud-ysql-cpp/">
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
    <a class="section-link icon-offset" href="csharp/cloud-ysql-csharp/">
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
    <a class="section-link icon-offset" href="ruby/cloud-ysql-ruby/">
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
    <a class="section-link icon-offset" href="rust/cloud-ysql-rust/">
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

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="php/cloud-ysql-php/">
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
    <a class="section-link icon-offset" href="scala/ycql/">
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

</div>

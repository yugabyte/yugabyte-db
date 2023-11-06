---
title: Drivers and ORMs for YugabyteDB
headerTitle: Drivers and ORMs
linkTitle: Drivers and ORMs
description: Connect your applications with supported drivers and ORMs
headcontent: Connect applications with your database
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
aliases:
  - /develop/client-drivers/
  - /preview/develop/client-drivers/
menu:
  preview:
    identifier: drivers-orms
    parent: develop
    weight: 570
type: indexpage
showRightNav: true
---

Applications connect to and interact with YugabyteDB using API client libraries (also known as client drivers). Because the YugabyteDB [YSQL API](../api/ysql/) is PostgreSQL-compatible, and the [YCQL API](../api/ycql/) has roots in the Apache Cassandra CQL, YugabyteDB supports many third-party drivers.

In addition to the compatible upstream PostgreSQL drivers, YugabyteDB also supports [smart drivers](smart-drivers/), which extend the PostgreSQL drivers to enable client applications to connect to YugabyteDB clusters without the need for external load balancers.

### Choose your language

<ul class="nav yb-pills">

  <li>
    <a href="java/" class="orange">
      <i class="fa-brands fa-java"></i>
      Java
    </a>
  </li>

  <li>
    <a href="go/" class="orange">
      <i class="fa-brands fa-golang"></i>
      Go
    </a>
  </li>

  <li>
    <a href="python/" class="orange">
      <i class="fa-brands fa-python"></i>
      Python
    </a>
  </li>

  <li>
    <a href="nodejs/" class="orange">
      <i class="fa-brands fa-node-js"></i>
      Node.js
    </a>
  </li>

  <li>
    <a href="c/" class="orange">
      <i class="icon-c"></i>
      C
    </a>
  </li>

  <li>
    <a href="cpp/" class="orange">
      <i class="icon-cplusplus"></i>
      C++
    </a>
  </li>

  <li>
    <a href="csharp/" class="orange">
      <i class="icon-csharp"></i>
      C#
    </a>
  </li>

  <li>
    <a href="ruby/" class="orange">
      <i class="icon-ruby"></i>
      Ruby
    </a>
  </li>

  <li>
    <a href="rust/" class="orange">
      <i class="fa-brands fa-rust"></i>
      Rust
    </a>
  </li>

  <li>
    <a href="php/" class="orange">
      <i class="fa-brands fa-php"></i>
      PHP
    </a>
  </li>

  <li>
    <a href="scala/" class="orange">
      <i class="icon-scala"></i>
      Scala
    </a>
  </li>

</ul>

## Supported libraries

The following libraries are officially supported by YugabyteDB.

{{< readfile "include-drivers-orms-list.md" >}}

## Read more

- [PostgreSQL drivers](https://www.postgresql.org/download/products/2-drivers-and-interfaces/)
- [Cassandra Query Language (CQL)](https://cassandra.apache.org/doc/latest/cassandra/cql/index.html)

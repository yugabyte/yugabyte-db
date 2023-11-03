---
title: Build applications using ORMs
headerTitle: Build applications using ORMs
linkTitle: Build apps using ORMs
description: Build a REST application using ORMs with YugabyteDB
headcontent: Learn how to use your favorite ORM with YugabyteDB
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    identifier: orm-tutorials
    parent: drivers-orms
    weight: 610
type: indexpage
showRightNav: true
---

An ORM tool is used to store the domain objects of an application into the database tables, handle database access, and map their object-oriented domain classes into the database tables. It simplifies the CRUD operations on your domain objects and allows the evolution of domain objects to be applied to the database tables.

The examples in this section illustrate how you can build a basic online e-commerce store application that connects to YugabyteDB by implementing a REST API server using the ORM of your choice.

The source for the REST API-based applications is in the [Using ORMs with YugabyteDB](https://github.com/yugabyte/orm-examples/tree/master) repository. Database access is managed using the ORM and includes the following tables:

- `users` — the users of the e-commerce site
- `products` — the products being sold
- `orders` — the orders placed by the users
- `orderline` — each line item of an order

### Choose your language

<ul class="nav yb-pills">

  <li>
    <a href="java/ysql-hibernate/" class="orange">
      <i class="fa-brands fa-java"></i>
      Java
    </a>
  </li>

  <li>
    <a href="go/ysql-gorm/" class="orange">
      <i class="fa-brands fa-golang"></i>
      Go
    </a>
  </li>

  <li>
    <a href="python/ysql-sqlalchemy/" class="orange">
      <i class="fa-brands fa-python"></i>
      Python
    </a>
  </li>

  <li>
    <a href="nodejs/ysql-sequelize/" class="orange">
      <i class="fa-brands fa-node-js"></i>
      Node.js
    </a>
  </li>

  <li>
    <a href="csharp/ysql-entity-framework/" class="orange">
      <i class="icon-csharp"></i>
      C#
    </a>
  </li>

  <li>
    <a href="rust/ysql-diesel/" class="orange">
      <i class="fa-brands fa-rust"></i>
      Rust
    </a>
  </li>

  <li>
    <a href="php/ysql-laravel/" class="orange">
      <i class="fa-brands fa-php"></i>
      PHP
    </a>
  </li>

</ul>

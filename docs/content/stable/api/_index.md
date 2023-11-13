---
title: API reference (for YSQL and YCQL)
headerTitle: APIs
linkTitle: APIs
description: YugabyteDB API reference for PostgreSQL-compatible YSQL and Cassandra-compatible YCQL
image: /images/section_icons/index/api.png
headcontent: YugabyteDB API reference
menu:
  stable:
    identifier: api
    parent: reference
    weight: 1100
type: indexpage
---

YugabyteDB supports two flavors of distributed SQL:

- [YSQL](ysql/) is a fully-relational SQL API that is wire compatible with the SQL language in PostgreSQL. It is best fit for RDBMS workloads that need horizontal write scalability and global data distribution while also using relational modeling features such as JOINs, distributed transactions and referential integrity (such as foreign keys).
- [YCQL](ycql/) is a semi-relational SQL API that is best fit for internet-scale OLTP and HTAP applications needing massive data ingestion and blazing-fast queries. It supports distributed transactions, strongly consistent secondary indexes and a native JSON column type. YCQL has its roots in the Cassandra Query Language.

Note that the APIs are isolated and independent from one another, and you need to select an API first before undertaking detailed database schema and query design and implementation.

<div class="row">

 <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="./ysql/">
      <div class="head">
        <img class="icon" src="/images/section_icons/api/ysql.png" aria-hidden="true" />
        <div class="title">YSQL reference</div>
      </div>
      <div class="body">
        API reference for Yugabyte Structured Query Language (YSQL).
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="./ycql/">
      <div class="head">
        <img class="icon" src="/images/section_icons/api/ycql.png" aria-hidden="true" />
        <div class="title">YCQL reference</div>
      </div>
      <div class="body">
        API reference for Yugabyte Cloud Query Language (YCQL).
      </div>
    </a>
  </div>

</div>

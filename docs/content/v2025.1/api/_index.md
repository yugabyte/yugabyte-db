---
title: YugabyteDB API reference (for YSQL and YCQL)
headerTitle: API
linkTitle: API
description: YugabyteDB API reference for PostgreSQL-compatible YSQL and Cassandra-compatible YCQL
headcontent: YugabyteDB API reference
type: indexpage
showRightNav: true
---
<!--menu:
  v2025.1_api:
    identifier: api
    parent: yugabyte-apis
    weight: 1100-->

## SQL APIs

YugabyteDB supports two flavors of distributed SQL:

- [YSQL](ysql/) is a fully-relational SQL API that is wire compatible with the SQL language in PostgreSQL. It is best fit for RDBMS workloads that need horizontal write scalability and global data distribution while also using relational modeling features such as JOINs, distributed transactions and referential integrity (such as foreign keys).
- [YCQL](ycql/) is a semi-relational SQL API that is best fit for internet-scale OLTP and HTAP applications needing massive data ingestion and blazing-fast queries. It supports distributed transactions, strongly consistent secondary indexes and a native JSON column type. YCQL has its roots in the Cassandra Query Language.

Note that the APIs are isolated and independent from one another, and you need to select an API first before undertaking detailed database schema and query design and implementation.

{{<index/block>}}

  {{<index/item
    title="YSQL reference"
    body="API reference for Yugabyte Structured Query Language (YSQL)."
    href="ysql/"
    icon="/images/section_icons/api/ysql.png">}}

  {{<index/item
    title="YCQL reference"
    body="API reference for Yugabyte Cloud Query Language (YCQL)."
    href="ycql/"
    icon="/images/section_icons/api/ycql.png">}}

{{</index/block>}}

## Client shells

YugabyteDB ships with command line interface (CLI) shells for interacting with each SQL API.

{{<index/block>}}

  {{<index/item
    title="ysqlsh reference"
    body="CLI for interacting with YugabyteDB using YSQL."
    href="ysqlsh/"
    icon="fa-light fa-terminal">}}

  {{<index/item
    title="ycqlsh reference"
    body="CLI for interacting with YugabyteDB using YCQL."
    href="ycqlsh/"
    icon="fa-light fa-terminal">}}

{{</index/block>}}

## Management APIs

YugabyteDB Anywhere and Aeon both provide APIs that can be used to deploy and manage universes, query system status, manage accounts, and more.

{{< sections/2-boxes >}}
  {{< sections/bottom-image-box
    title="YugabyteDB Anywhere API"
    description="Manage YugabyteDB Anywhere using the API."
    buttonText="API Documentation"
    buttonUrl="https://api-docs.yugabyte.com/docs/yugabyte-platform/f10502c9c9623-yugabyte-db-anywhere-api-overview"
  >}}

  {{< sections/bottom-image-box
    title="YugabyteDB Aeon API"
    description="Manage YugabyteDB Aeon using the API."
    buttonText="API Documentation"
    buttonUrl="https://api-docs.yugabyte.com/docs/managed-apis/9u5yqnccbe8lk-yugabyte-db-aeon-rest-api"
  >}}

{{< /sections/2-boxes >}}

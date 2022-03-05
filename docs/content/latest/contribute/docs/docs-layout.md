---
title: Find the right page or section
headerTitle: Find the right page or section
linkTitle: Docs layout
description: Find the right location in the YugabyteDB docs
image: /images/section_icons/index/quick_start.png
type: page
menu:
  latest:
    identifier: docs-layout
    parent: docs
    weight: 2912
isTocNested: true
showAsideToc: true
---

The YugabyteDB docs are divided into several sections:

* [**YugabyteDB Core**](/latest/) is the overview documentation for YugabyteDB
* [**Yugabyte Platform**](/latest/yugabyte-platform/) documents Yugabyte Platform
* [**Yugabyte Cloud**](/latest/yugabyte-cloud/) documents Yugabyte Cloud
* [**Releases**](/latest/releases/) contains release notes and other information related to releases
* [**Integrations**](/latest/integrations/) documents third-party integrations
* [**Reference**](/latest/reference/configuration/) contains detailed reference and architecture information about functions, features, and interfaces
* [**FAQ**](/latest/faq/general/) contains frequently-asked questions on a variety of topics
* [**Misc**](/latest/legal/) contains legal information and the (deprecated) YEDIS subsystem

### YugabyteDB core docs

The [core docs](/latest/) are landing pages with overview and getting-started information. Pages in this section should have a high-level overview of a feature, what's supported, limitations, and a link to any roadmap GitHub issues. These docs pages can have "Further reading" sections, and we should add links to blogs or Reference section docs as appropriate.

#### Explore section

Think of the pages in the [Explore section](/latest/explore/) as a self-guided tour of YugabyteDB. When you're reading a page in this section, you should be able to get a good sense of how the feature works. The page may not answer every question you have, but should point you to the reference page where you can find that information.

### Reference docs

Reference docs should be comprehensive and, above all, accurate. This applies to other doc types, but is especially important for reference docs, as they should be the ultimate source of truth for users.

Here are some examples of reference docs in our documentation:

* [Replication in DocDB](https://docs.yugabyte.com/latest/architecture/docdb-replication/replication/)
* SQL reference sample: [CREATE TABLE [YSQL]](https://docs.yugabyte.com/latest/api/ysql/the-sql-language/statements/ddl_create_table/)

### Design docs on GitHub

We also have design docs [in GitHub](https://github.com/yugabyte/yugabyte-db/tree/master/architecture/design). These design docs should be referenced from the Reference section in the docs.

## Next steps

Now that you know where your page should go, [build the docs](../docs-build/) locally and [start editing](../docs-edit/).

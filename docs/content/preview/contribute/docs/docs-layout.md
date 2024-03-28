---
title: Find the right page or section
headerTitle: Find the right page or section
linkTitle: Docs layout
description: Find the right location in the YugabyteDB docs
menu:
  preview:
    identifier: docs-layout
    parent: docs
    weight: 2912
type: docs
---

The YugabyteDB docs are divided into several sections:

* [**YugabyteDB Core**](/preview/) is the overview documentation for YugabyteDB
* [**YugabyteDB Anywhere**](/preview/yugabyte-platform/) documents YugabyteDB Anywhere
* [**YugabyteDB Managed**](/preview/yugabyte-cloud/) documents YugabyteDB Managed
* [**Releases**](/preview/releases/) contains release notes and other information related to releases
* [**Integrations**](/preview/integrations/) documents third-party integrations
* [**Reference**](/preview/reference/configuration/) contains detailed reference and architecture information about functions, features, and interfaces
* [**FAQ**](/preview/faq/general/) contains frequently-asked questions on a variety of topics
* [**Misc**](/preview/legal/) contains legal information and the (deprecated) YEDIS subsystem

### YugabyteDB core docs

The [core docs](/preview/) are landing pages with overview and getting-started information. Pages in this section should have a high-level overview of a feature, what's supported, limitations, and a link to any roadmap GitHub issues. These docs pages can have "Further reading" sections, where you can add links to blogs or Reference section docs as appropriate.

#### Explore section

Think of the pages in the [Explore section](/preview/explore/) as a self-guided tour of YugabyteDB. When you're reading a page in this section, you should be able to get a good sense of how the feature works. The page may not answer every question you have, but should point you to the reference page where you can find that information.

### Reference docs

Reference docs should be comprehensive and, above all, accurate. This applies to other doc types, but is especially important for reference docs, as they should be the ultimate source of truth for users.

Here are some examples of reference docs in our documentation:

* [Replication in DocDB](/preview/architecture/docdb-replication/replication/)
* SQL reference sample: [CREATE TABLE [YSQL]](/preview/api/ysql/the-sql-language/statements/ddl_create_table/)

### Design docs on GitHub

We also have design docs [in GitHub](https://github.com/yugabyte/yugabyte-db/tree/master/architecture/design). These design docs should be referenced from the Reference section in the docs.

## Legend for illustrations

Many of the illustrations in the docs use the following legend to represent tablet leaders and followers, cloud regions and zones, and applications.

![Legend for illustrations](/images/develop/global-apps/global-database-legend.png)

## Next steps

Now that you know where your page should go, [build the docs](../docs-build/) locally and [start editing](../docs-edit/).

---
title: Sample datasets
linkTitle: Sample datasets
description: Sample datasets
headcontent: Explore the YugabyteDB YSQL API using sample datasets.
menu:
  stable:
    identifier: sample-data
    parent: reference
    weight: 2950
type: indexpage
---

YugabyteDB and YugabyteDB client shell installations include sample datasets you can use to test out YugabyteDB. These are located in the `share` directory of your installation. The datasets are also available in the [sample directory of the YugabyteDB GitHub repository](https://github.com/yugabyte/yugabyte-db/tree/master/sample). The datasets are provided in the form of SQL script files. All of the datasets are PostgreSQL-compatible, and you can explore them using the [ysqlsh shell](../api/ysqlsh/) to compare between PostgreSQL and the YugabyteDB [YSQL API](../api/ysql/).

You can install and use the sample datasets using either a local installation of YugabyteDB, or by connecting to a cluster in YugabyteDB Aeon (including your free cluster).

Local install
: The ysqlsh shell is included with the YugabyteDB installation. For information on installing YugabyteDB, refer to [Quick start](/stable/quick-start/macos/).

YugabyteDB Aeon
: For information on connecting to your YugabyteDB Aeon cluster using ysqlsh in cloud shell, refer to [Connect using cloud shell](../yugabyte-cloud/cloud-connect/connect-cloud-shell/).
: For information on connecting to your YugabyteDB Aeon cluster using the ysqlsh client installed on your computer, refer to [Connect via client shells](/stable/yugabyte-cloud/cloud-connect/connect-client-shell/).
: The exercises can be run on free or standard clusters. To get started with YugabyteDB Aeon, refer to [Quick start](/stable/yugabyte-cloud/cloud-quickstart/).

{{<index/block>}}

  {{<index/item
    title="Chinook"
    body="Explore the popular sample dataset for a digital media store."
    href="chinook/"
    icon="/images/section_icons/sample-data/s_s2-chinook-3x.png">}}

  {{<index/item
    title="Northwind"
    body="Explore the classic sales datasets for Northwind Traders."
    href="northwind/"
    icon="/images/section_icons/sample-data/s_s3-northwind-3x.png">}}

  {{<index/item
    title="PgExercises"
    body="Learn SQL and test your knowledge by creating the PostgreSQL Exercises database and trying out the available exercises."
    href="pgexercises/"
    icon="/images/section_icons/sample-data/s_s4-pgexercises-3x.png">}}

  {{<index/item
    title="SportsDB"
    body="Explore sample sports statistics for baseball, football, basketball, ice hockey, and soccer."
    href="sportsdb/"
    icon="/images/section_icons/sample-data/s_s5-sportsdb-3x.png">}}

  {{<index/item
    title="Retail Analytics"
    body="Ad-hoc analytics of retail sales data using YugabyteDB's YSQL API."
    href="retail-analytics/"
    icon="/images/section_icons/develop/apps/e-commerce.png">}}

{{</index/block>}}

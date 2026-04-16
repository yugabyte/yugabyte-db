---
title: Hevo Data
linkTitle: Hevo Data
description: Use Hevo Data with YSQL API
aliases:
menu:
  preview_integrations:
    identifier: hevodata
    parent: data-integration
    weight: 571
type: docs
---


[Hevo Data](https://hevodata.com/) is a no-code data movement platform which lets you set up your database or data warehouse for analytics and is designed to process billions of records.
After all the historical data in the source is ingested, any new and changed data thereafter is ingested as incremental data. In case of RDBMS sources like PostgreSQL, you can define how Hevo fetches this data, using ingestion modes, and query modes.

Hevo ingests your data from [Sources](https://docs.hevodata.com/sources/), applies transformations on it, and brings it to a [Schema Mapper](https://docs.hevodata.com/pipelines/schema-mapper/) using which you can define how your data must be stored at the [Destination](https://docs.hevodata.com/destinations/).

Hevo supports PostgreSQL among many other databases as a source. It can also work with YugabyteDB as a source using PostgreSQL's configuration with some changes.

## Connect

To add YugabyteDB as a source in Hevo, create a [Pipeline](https://docs.hevodata.com/pipelines/) as follows:

1. Start a YugabyteDB cluster. Refer [YugabyteDB Prerequisites](../tools/#yugabytedb-prerequisites).
1. In the Hevo UI, create a new Pipeline by selecting PostgreSQL as the source type.
1. Provide an appropriate hostname and port (Default for YugabyteDB is 5433) for your YugabyteDB cluster as per the following illustration:

   ![pipeline](/images/section_icons/develop/ecosystem/hevodata-setup.png)

1. Under **Select an Ingestion Mode**, choose **Table**. You can also choose **Custom SQL**, in which case you also need to provide the actual SQL to fetch data from your tables. **Logical Replication** is not supported.
1. Provide the database and schema name where the table data is present and other connection details.
1. Select the tables which you want to ingest into Hevo.
1. Select a value for the **Query Mode**. If your table has an incrementing column and a timestamp column which gets updated for updates, you can choose **Change Data Capture**. **xmin** is not supported in YugabyteDB.
1. Select and configure a destination as per your requirement.

After the pipeline is created, data from your YugabyteDB cluster gets ingested into the destination via the Hevo pipeline.


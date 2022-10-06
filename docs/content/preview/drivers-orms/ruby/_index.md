---
title: Ruby
headerTitle: Ruby
linkTitle: Ruby
description: Ruby Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    identifier: ruby-drivers
    parent: drivers-orms
    weight: 590
type: indexpage
---

The following projects are recommended for implementing Ruby applications using the YugabyteDB YSQL/YCQL API.

## Supported projects

| Project | Example Apps |
| :------ | :----------- |
| Pg Gem Driver | [Hello World](ysql-pg/) |
| YugabyteDB Ruby Driver for YCQL | [Hello World](ycql/) </br></br> [Sample apps](https://github.com/yugabyte/cassandra-ruby-driver) |
| ActiveRecord ORM | [Hello World](activerecord/) </br></br> [Sample apps](https://github.com/YugabyteDB-Samples/orm-examples/tree/master/ruby/ror)|

For fully-runnable code snippets and explanations of common operations, see the **example apps**. Before running the example apps, make sure you have installed the prerequisites.

## Prerequisites

To develop Ruby applications for YugabyteDB, you need:

- **a YugabyteDB cluster**
  - Create a free cluster on [YugabyteDB Managed](https://www.yugabyte.com/cloud/). Refer to [Use a cloud cluster](../../quick-start-yugabytedb-managed/). Note that YugabyteDB Managed requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](../../quick-start/).


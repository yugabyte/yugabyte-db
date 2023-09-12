---
title: Time series data model
headerTitle: Time series data model
linkTitle: Time series
description: Explore the Time series data model
headcontent: Handle large amounts of data ordered by time
menu:
  preview:
    identifier: common-patterns-timeseries
    parent: common-patterns
    weight: 100
type: indexpage
---

Time series data are measurements or events that are tracked and monitored over time. This could be server metrics, application performance monitoring, network data, sensor data, events, clicks, trades in a market, and many other types of analytics data. A time series data model is designed specifically for handling large amounts of data that are ordered by time.

Although YugabyteDB is hash sharded by default, it also supports range sharding, where the data is ordered and split at specific boundaries.

A time series pattern works best for range queries where you need to look up items in a given time range.

You can use the following common patterns to store and retrieve time series data in YugabyteDB in both distributed and ordered manner:

- **Global ordering by time**

    In this pattern, all your data is ordered by time across different tablets.

    To understand how to efficiently store and retrieve data in this pattern, see [Global ordering by time](./global-ordering).

- **Ordering by entity**

    In this pattern, the data is ordered by time in a specific entity.

    To understand how to distribute the entities effectively and avoid hot shards, see [Ordering by entity](./ordering-by-entity).

- **Automatic data expiration**

    In some scenarios, you don't want data lying around for a long time as they may not be needed or you have rules in your organization that you cannot store specific data longer than a particular duration. For such cases, you can set a time-to-live value on rows, columns, and the table itself.

    For more details, see [Automatic data expiration](./data-expiry).

- **Partitioning**

    When you have a lot of data that needs to deleted regularly, you can opt to partition your data. This also has speed advantages in some cases.

    For more details, see [Partitioning by time](./partitioning-by-time).

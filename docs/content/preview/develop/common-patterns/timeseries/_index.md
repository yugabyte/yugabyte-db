---
title: Timeseries data model
headerTitle: Timeseries data model
linkTitle: Timeseries
description: Explore the Timeseries data model
headcontent: Explore the Timeseries data model
menu:
  preview:
    identifier: common-patterns-timeseries
    parent: common-patterns
    weight: 100
type: indexpage
---

Timeseries data are measurements or events that are tracked and monitored over time. This could be server metrics, application performance monitoring, network data, sensor data, events, clicks, trades in a market, and many other types of analytics data. A timeseries data model is designed specifically for handling large amounts of data that are ordered by time.

Although YugabyteDB is hash sharded by default, it also supports range sharding, where the data is ordered and split at specific boundaries.

A timeseries pattern works best for range queries where you need to look up items in a given time range.

Explore the different ways to store and retrieve timeseries data in YugabyteDB in both distributed and ordered manner using some common patterns typically used when data is modeled as a timeseries in the following sections.

## Global ordering by time

In this pattern, all your data is ordered by time across different tablets. To understand how to efficiently store and retrieve data in this pattern, see [Global ordering by time](./global-ordering).

## Ordering by entity

In this pattern, the data is ordered by time in a specific entity. To understand how to distribute the entities effectively and avoid hot shards, see [Ordering by entity](./ordering-by-entity).

## Automatic data expiration

There are scenarios where you do not want data lying around for a long time as they may not be needed or you have rules in your organization that you cannot store specific data longer than a particular duration. For such cases, you can set a time-to-live value on rows, columns, and the table itself.

For more details, see [Automatic data expiration](./data-expiry).

## Partitioning

When there is a lot of data that needs to deleted regularly, then you can opt to partition your data. This also has speed advantages in some cases.

For more details, see [Partitioning by time](./partitioning-by-time).

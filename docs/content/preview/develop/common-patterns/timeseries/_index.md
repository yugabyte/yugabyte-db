---
title: Timeseries Data Model
headerTitle: Timeseries Data Model
linkTitle: Timeseries
description: Explore the Timeseries Data Model
headcontent: Explore the Timeseries Data Model
menu:
  preview:
    identifier: common-patterns-timeseries
    parent: common-patterns
    weight: 100
type: indexpage
---

Time series data are simply measurements or events that are tracked and monitored over time. This could be server metrics, application performance monitoring, network data, sensor data, events, clicks, trades in a market, and many other types of analytics data. A time series data model is designed specifically for handling large amounts of data that are ordered by time.

Although YugabyteDB is hash sharded by default, it also supports range sharding, where the data is ordered and split at specific boundaries. Let us explore the different ways to store and retrieve time-series data in YugabyteDB in both distributed and ordered manner. Let us go over some common patterns typically used when data is modeled as a time series.

A time series pattern works best for range queries where you need to look up items in a given time range.

## Global ordering by time

In this pattern, all your data is ordered by time across different tablets. To understand how to efficiently store and retrieve data in this pattern see, [Global ordering](./global-ordering)

## Ordering by entity

In this pattern, the data is ordered by time within a specific entity. To understand how to distribute the entities effectively and avoid hot shards see, [Ordering by entity](./ordering-by-entity)

## Automatic data expiration

There are scenarios where you do not want data lying around for a long time as they may not be needed or you have rules within your organization that you cannot store a specific data longer than a particular duration. For this you can set a time-to-live value on rows,columns and the table itself. See [Automatic data expiration](./data-expiry.md) for more info.

## Partitioning

When there is a lot of data that needs to deleted regularly, then you can opt to partition your data. This also has speed advantages in some cases. See [Partitioning by time](./partitioning-by-time.md) for more info.
---
title: Partitioning tables in YugabyteDB
headertitle: Partitioning tables
linkTitle: Partitioning tables
badges: ysql
menu:
  preview:
    identifier: data-modeling-partitions
    parent: data-modeling
    weight: 500
type: docs
---


[Data partitioning](../../../explore/ysql-language-features/advanced-features/partitions) refers to the process of dividing a large table or dataset into smaller physical partitions based on certain criteria or rules. This technique offers several benefits, including improved performance, easier data management, and better use of storage resources. Each partition is internally a table. This scheme is useful for managing large volumes of data and particularly for dropping older data.

### Manage large datasets

You can manage large data volumes by partitioning based on time (say by day, week, month, and so on) to make it easier to drop old data, especially when you want to retain only the recent data.

![Table partitioning](/images/develop/data-modeling/table-partitioning.png)

{{<lead link="../../common-patterns/timeseries/partitioning-by-time">}}
To understand how large data can be partitioned for easier management, see [Partitioning data by time](../../common-patterns/timeseries/partitioning-by-time).
{{</lead>}}

### Place data closer to users

When you want to improve latency for local users when your users are spread across a large geography, partition your data according to where big clusters of users are located, and place their data in regions closer to them using [tablespaces](../../../explore/going-beyond-sql/tablespaces). Users will end up talking to partitions closer to them.

![East and west applications](/images/develop/global-apps/latency-optimized-geo-partition-final.png)

{{<lead link="../../build-global-apps/latency-optimized-geo-partition">}}
To understand how to partition and place data closer to users for improved latency, see [Latency-optimized geo-partitioning](../../build-global-apps/latency-optimized-geo-partition).
{{</lead>}}

### Adhere to compliance laws

You can partition your data according to the user's citizenship and place their data in the boundaries of their respective nations to be compliant with data residency laws like [GDPR](https://en.wikipedia.org/wiki/General_Data_Protection_Regulation).

![User data stored within their country's boundaries](/images/develop/global-apps/locality-optimized-geo-partition-goal.png)

{{<lead link="../../build-global-apps/locality-optimized-geo-partition">}}
To understand how to partition data to be compliant with data residency laws, see [Locality-optimized geo-partitioning](../../build-global-apps/locality-optimized-geo-partition).
{{</lead>}}

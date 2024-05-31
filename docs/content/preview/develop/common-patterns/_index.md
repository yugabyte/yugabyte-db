---
title: Common patterns
headerTitle: Common patterns
linkTitle: Common patterns
description: Build on Common patterns
headcontent: Design robust and efficient applications using common patterns
image: /images/section_icons/architecture/distributed_acid.png
menu:
  preview:
    identifier: common-patterns
    parent: develop
    weight: 400
type: indexpage
showRightNav: true
---

YugabyteDB is a distributed database that provides data access via the YSQL and YCQL APIs. Although it supports these complex APIs, underneath it is a NoSQL store. This makes YugabyteDB a natural fit for multiple data models such as time series, key-value, and wide-column.

The following sections describe how you can leverage common data models to design robust and efficient applications.

## Time series

The time series data model meets the special needs of large event data scenarios for preserving event ordering and massive storage. The time series is effectively a sequence of events or messages ordered by time. The event data can be variable in size and YugabyteDB handles large amounts of data excellently. In YugabyteDB, the data is sorted and written sequentially to disk. When retrieving data by row key and then by range, you get a fast and efficient access pattern, due to minimal disk seeks. time series data is an excellent fit for this type of pattern.

A good example would be the speed sensor in a car that tracks the speed of a car and sends the data to a remote system for tracking.

```sql{.nocopy}
"car1" , "2023-05-01 01:00:00", 35
"car1" , "2023-05-01 01:01:00", 40
"car1" , "2023-05-01 01:02:00", 42
"car2" , "2023-05-06 01:00:00", 60
"car2" , "2023-05-06 01:01:00", 65
"car2" , "2023-05-06 01:01:00", 70
```

An insurance company could use the data to investigate accidents or an automobile company could track various sensors and improve the performance of the car. This could amount to billions of data points.

{{<lead link="./timeseries">}}
For more information on storing and retrieving such vast amounts of ordered data in YugabyteDB, see [Time series data model](./timeseries).
{{</lead>}}

## Key-value

In the key-value data model, each key is associated with one and only one value. YugabyteDB internally stores data as a collection of key-value pairs and hence automatically excels as a key-value store.

Because in a key-value store, each key has exactly one value, keys are typically defined as a combination of multiple parameters. For example, to store the details of a user, you could adopt the following schema:

```json
user1.name = "John Wick"
user1.country = "USA"
user2.name = "Harry Potter"
user2.country = "UK"
```

Key-value stores are expected to be some of the fastest storage data models.

{{<lead link="./keyvalue">}}
For more information on using YugabyteDB for key-value stores, see [Key-value data model](./keyvalue).
{{</lead>}}

## Wide-column

In a wide-column data model, the data is organized as rows and columns. Each row is identified by a row `id` or `name` and each column is identified by a column `id` or `name`. Each row can have any number of columns attached to it. You can visualize it as a table-like structure where some of the cells are empty. For example:

```caddyfile{.nocopy}
|       | col-1 | col-2 | col-3 |
| ----- | ----- | ----- | ----- |
| row-1 | a     |       | c     |
| row-2 | e     | f     | g     |
| row-3 | z     |       |       |
```

To retrieve specific cells, you can issue commands similar to the following:

```python{.nocopy}
get(row-1, col-3) ==> c
get(row-3, col-2) ==> NULL
```

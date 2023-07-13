---
title: Common Patterns
headerTitle: Common Patterns
linkTitle: Common Patterns
description: Build on Common Patterns
headcontent: Build on Common Patterns
image: /images/section_icons/architecture/distributed_acid.png
menu:
  preview:
    identifier: common-patterns
    parent: develop
    weight: 400
type: indexpage
showRightNav: true
---

YugabyteDB is a distributed database that provides data access via two apis - YSQL and YCQL. Although it supports such complex APIs, underneath it is a NoSQL store. This has enabled YugabytedDB to be a natural fit for multiple data models like Timeseries, Key-Value, Wide Column etc. Let us look into each of these common data models.

## Timeseries

The Timeseries data model meets the special needs of large event data scenarios for preserving event ordering and massive storage. The Timeseries is effectively a sequence of events or messages ordered by time. The event data can be variable in size and YugabyteDB handles large amounts of data excellently. In Yugabyted the data is sorted and written sequentially to disk. When retrieving data by row key and then by range, you get a fast and efficient access pattern, due to minimal disk seeks. Time series data is an excellent fit for this type of pattern. For example, A good example would be the speed sensor in a car that tracks the speed of a car and sends them to a remote system for tracking.

```sql{class=nocopy}
"car1" , "2023-05-01 01:00:00", 35 
"car1" , "2023-05-01 01:01:00", 40 
"car1" , "2023-05-01 01:02:00", 42 
"car2" , "2023-05-06 01:00:00", 60 
"car2" , "2023-05-06 01:01:00", 65 
"car2" , "2023-05-06 01:01:00", 70 
```

An insurance company could use these data to investigate accidents or an automobile company could track various sensors and improve the performance of the car. This could amount to billions of data points. For more information on storing and retrieving such vast amounts of ordered data, see [Timeseries](./timeseries).

## Key-Value

In the Key-Value data model, each key is associated with one and only one value. YugabyteDB internally stored data as a collection of key-value pairs and hence automatically excels as Key-Value store. Because in a key-value store, each key has exactly one value, it is typical to define the key as a combination of multiple parameters. For example to store the details of a user, one may adopt the following schema:

```json
user1.name = "John Wick"
user1.country = "USA"
user2.name = "Harry Potter"
user2.country = "UK"
```

Key Value stores are expected to be some of the fastest storage data models.

## Wide-column

In a wide-column data model, the data is organized as rows and columns. Each row is identified by a row `id` or `name` and each column is identified by a column -`id` or `name`. Each row can have any no. of columns attached to it. You can visualize it to be a table like structure where some of the cells are empty. For example,

```sql{class=nocopy}
|       | col-1 | col-2 | col-3 |
| ----- | ----- | ----- | ----- |
| row-1 | a     |       | c     |
| row-2 | e     | f     | g     |
| row-3 | z     |       |       |
```

To get specific cells, you can issue commands like:

```sql{class=nocopy}
get(row-1, col-3) ==> c
get(row-3, col-2) ==> NULL
```

## Learn more

- [Timeseries](./timeseries)
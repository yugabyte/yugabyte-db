---
title: Learn application development
linkTitle: Learn app development
description: Learn to develop YugabyteDB applications
image: /images/section_icons/develop/learn.png
aliases:
  - /develop/learn/
menu:
  preview:
    identifier: learn
    parent: develop
    weight: 560
type: indexpage
---

## Transactions

Transactions are a sequence of operations performed as a single logical unit of work. These operations can modify multiple tables or rows. Transactions are important to maintain data integrity when multiple users are modifying the same set of rows across tables. Eg. Credit/Debit from a bank account.

{{<lead link="./transactions/acid-transactions-ysql">}}
To understand how to use transactions when developing applications, see [Transactions](./transactions/acid-transactions-ysql)
{{</lead>}}

## Text search

YugabyteDB supports advanced text search schemes like similarity search, phonetic search and full-text search along with the standard pattern matching with the `LIKE` operator.

{{<lead link="./transactions/acid-transactions-ysql">}}
To understand build advanced search functionalities into your applications, see [Text search](./text-search/)
{{</lead>}}

## Aggregations

When performing analytical operations on your data, it is common to fetch aggregates like min, max, sum, average etc.

{{<lead link="./aggregations-ycql">}}
To understand how to best use aggregates in your applications, see [Aggregations](./aggregations-ycql)
{{</lead>}}

## Batch operations

It is sometimes better to batch multiple statements into one request to avoid round trips to the server. In [YSQL](/{{<version>}}/api/ysql), this can be accomplished with [Stored Procedures](/{{<version>}}/explore/ysql-language-features/stored-procedures/), which are not supported in the [YCQL](/{{<version>}}/api/ycql) API.

{{<lead link="./batch-operations-ycql">}}
To understand how to best do batch operations in YCQL, see [Batch operations](./batch-operations-ycql)
{{</lead>}}

## Date and time

Although date and time are common concepts, working with dates and times across various time zones can be quite a challenge. It is important that you understand the various formatting and functionalities around these important data types.

{{<lead link="./date-and-time-ysql">}}
To understand how to use date and time data types effectively in your applications, see [Date and Time](./date-and-time-ysql)
{{</lead>}}

## Strings and text

Text, String and character data types are probably some of the most commonly used types when designing a schema. YugabyteDB provides an extensive suite of functionality to format and manipulate text data types.

{{<lead link="./strings-and-text-ysql">}}
To use text, string and character types effectively in your applications, see [Strings and text](./strings-and-text-ysql)
{{</lead>}}

## Data expiration

Cleaning up old, unwanted data can be a painful task. YugabyteDB supports Time-to-Live (TTL) functionality in the YCQL API which can be very useful to automatically purge old data and reduce storage costs.

{{<lead link="./ttl-data-expiration-ycql">}}
To understand how to use TTL in your YCQL applications, see [TTL for data expiration](./ttl-data-expiration-ycql)
{{</lead>}}


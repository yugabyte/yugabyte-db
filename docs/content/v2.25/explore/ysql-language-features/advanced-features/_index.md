---
title: Advanced features
linkTitle: Advanced features
description: Advanced features in YSQL
headcontent:  Explore advanced features in YSQL
menu:
  v2.25:
    identifier: advanced-features
    parent: explore-ysql-language-features
    weight: 900
type: indexpage
---

## Collations

Collations define the rules for how string data is sorted and compared in a database. They determine the order of characters, case sensitivity, and accent sensitivity, which can vary based on language and locale. By specifying a collation, you can ensure that text data is handled in a way that aligns with the linguistic and cultural expectations of your users. Collations are essential for accurate sorting and searching of text data.

{{<lead link="collations/">}}
To understand how to use collations correctly for your data, see [Collations](collations/)
{{</lead>}}

## Cursors

Cursors are database objects used to retrieve, manipulate, and navigate through a result set row by row. Cursors allow you to fetch rows sequentially, move to specific rows, and perform updates or deletions on the current row. While powerful, cursors should be used judiciously, as they can be resource-intensive and may impact performance if not managed properly.

{{<lead link="cursor/">}}
To understand how to create and operate on cursors, see [Cursors](cursor/)
{{</lead>}}

## Foreign data wrappers

Foreign data wrappers (FDWs) allow YugabyteDB to access and interact with external data sources as if they were local tables. This capability enables seamless integration of diverse data sources, such as other databases, files, or web services, into your SQL queries. FDWs provide a standardized way to query and manipulate external data, making it easier to combine and analyze information from multiple systems.

{{<lead link="foreign-data-wrappers/">}}
To understand how to extend the reach of your database, enhance data integration, and streamline workflows, see [Foreign data wrappers](foreign-data-wrappers/)
{{</lead>}}

## Savepoint

Savepoints are markers in a transaction that allow you to roll back part of the transaction without affecting the entire transaction. They are particularly useful in long transactions where multiple operations are performed, as they enable finer control over the transaction's execution and help maintain data integrity by allowing partial rollbacks.

{{<lead link="savepoints/">}}
To understand how to use savepoints, see [Savepoints](savepoints/)
{{</lead>}}

## Stored procedures

Stored procedures are precompiled collections of SQL statements and optional control-of-flow statements, stored on the server side. They allow you to encapsulate complex business logic and database operations into reusable scripts that can be executed with a single call. Stored procedures enhance performance by reducing the amount of information sent between the client and server.

{{<lead link="stored-procedures/">}}
To understand how to create and use stored procedures, see [Stored procedures](stored-procedures/)
{{</lead>}}

## Table partitioning

Table partitioning is a database optimization technique that divides a large table into smaller, more manageable pieces called partitions. Each partition can be managed and accessed independently, which can significantly improve query performance and simplify maintenance tasks. Partitioning can be based on various criteria, such as ranges of values, lists of values, or hash functions.

{{<lead link="partitions/">}}
To understand how to create and manage partitions, see [Table partitions](partitions/)
{{</lead>}}

## Triggers

Triggers are special types of stored procedures that automatically execute in response to certain events on a table or view. These events can include insertions, updates, or deletions of data. Triggers are used to enforce business rules, maintain data integrity, and automate system tasks. By defining triggers, you can ensure that specific actions are taken automatically when certain conditions are met, such as logging changes, validating data, or updating related tables.

{{<lead link="triggers/">}}
To understand how to use triggers effectively in your applications, see [Triggers](triggers/)
{{</lead>}}

## Views

Views are virtual tables that present a customized view of data from underlying tables. They are defined by a SELECT statement and can be used to simplify complex queries, restrict access to certain data, or provide a more user-friendly interface.

{{<lead link="views/">}}
To understand how to create and operate on views, see [Views](views/)
{{</lead>}}

## Synchronizing snapshots

Snapshot synchronization ensures that two or more independent, concurrently running transactions share the same consistent view of the data.

{{<lead link="snapshot-synchronization/">}}
To learn how different transactions can maintain a consistent view of the data, see [Synchronize snapshots](snapshot-synchronization/)
{{</lead>}}

## Inheritance

Table inheritance lets you create child tables that automatically share columns and constraints from a parent table.

{{<lead link="inheritance/">}}
To see how to create tables that inherit from a parent table, see [Inheritance](inheritance/)
{{</lead>}}

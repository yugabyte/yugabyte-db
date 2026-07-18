---
title: Indexes
headerTitle: Indexes
linkTitle: Indexes
description: Using indexes in YugabyteDB YSQL
headcontent: Explore indexes and constraints in YSQL
menu:
  v2.25:
    identifier: explore-indexes-constraints-ysql
    parent: explore-ysql-language-features
    weight: 700
type: indexpage
---

Indexes are powerful tools designed to improve the speed of data retrieval operations by creating efficient pathways to access the data in a table. Similar to an index in a book, SQL indexes allow the database to quickly locate the desired rows without scanning the entire table. While indexes enhance query performance, they can also impact the speed of INSERT, UPDATE, and DELETE operations. In YugabyteDB, indexes are treated internally as tables, and just like tables, they are distributed and stored in [LSM](https://en.wikipedia.org/wiki/Log-structured_merge-tree) format, as opposed to the [B-tree](https://www.postgresql.org/docs/current/btree-implementation.html#BTREE-STRUCTURE) structure used by indexes in PostgreSQL.

{{<note>}}
The sharding of indexes is based on the primary key of the index and is independent of how the main table is sharded/distributed. Indexes are not colocated with the base table.
{{</note>}}

## Primary key

A primary key is a unique identifier assigned to each row in a relational database table. It ensures that every record has a distinct value, preventing duplicate entries. Primary keys are crucial for maintaining data integrity and enabling efficient data retrieval.

{{<lead link="primary-key-ysql/">}}
To understand how to define primary keys and choose the right columns, see [Primary keys](primary-key-ysql/)
{{</lead>}}

## Secondary index

Secondary indexes are additional indexes created on columns other than the primary key. They enhance query performance by allowing faster data retrieval based on non-primary key columns, speeding up search queries, filtering, and sorting operations. Unlike primary keys, which enforce uniqueness and identify each row uniquely, secondary indexes can be created on columns that may contain duplicate values.

{{<lead link="secondary-indexes-ysql/">}}
To understand how to use indexes for faster retrieval, see [Secondary indexes](secondary-indexes-ysql/)
{{</lead>}}

## Unique index

Unique index enforces uniqueness, preventing duplicate entries and maintaining data integrity. When a unique index is applied to a column, the database automatically checks for duplicate values and rejects any insert or update operations that would violate this constraint.

{{<lead link="unique-index-ysql/">}}
To understand how to use unique indexes to maintain data integrity, see [Unique indexes](unique-index-ysql/)
{{</lead>}}

## Partial indexes

Partial indexes are specialized indexes that include only a subset of rows in a table, based on a specified condition. This type of index is particularly useful for optimizing queries that frequently access a specific portion of the data. By indexing only the rows that meet the condition, partial indexes can reduce storage requirements and improve query performance.

{{<lead link="partial-index-ysql/">}}
To understand how to use partial indexes to save space and speed up queries, see [Partial indexes](partial-index-ysql/)
{{</lead>}}

## Covering index

Covering indexes include all the columns needed to satisfy a query, allowing the database to retrieve the required data directly from the index without accessing the table itself. This can significantly improve query performance by reducing the trip to the table.

{{<lead link="covering-index-ysql/">}}
To understand how to use covering indexes to speed up queries, see [Covering indexes](covering-index-ysql/)
{{</lead>}}

## Expression index

Expression indexes are created on a calculated expression rather than a simple column. This allows you to index the result of a function or expression, providing efficient access to data based on the calculated value.

{{<lead link="expression-index-ysql/">}}
To understand how to use expression indexes in your data model, see [Expression indexes](expression-index-ysql/)
{{</lead>}}

## GIN index

Generalized Inverted Indexes (GIN) are specialized indexes designed to handle complex data types and full-text search efficiently. GIN indexes are particularly effective for indexing columns that contain composite values, such as arrays, JSONB, and full-text documents.

{{<lead link="gin/">}}
To understand how to use GIN indexes in your data model, see [GIN indexes](gin/)
{{</lead>}}

## Index backfill

Index backfill refers to the process of populating an index with existing data after the index has been created. This is particularly important in large databases where creating an index can be time-consuming and resource-intensive. This allows for continued read and write operations on the table, minimizing downtime and maintaining database performance.

{{<lead link="index-backfill/">}}
To understand how index backfill works, see [Index backfill](index-backfill/)
{{</lead>}}

## Unsupported indexes

Generalized Search Tree (GiST) indexes in SQL are versatile indexes that support a wide range of query types and data structures. GiST indexes are particularly useful for indexing complex data types such as geometric shapes, text search, and custom data types. GiST indexes are **not** supported in YugabyteDB; follow {{<issue 1337>}} for updates.

## Check indexes

{{<tags/feature/ea idea="2160">}} Use the `yb_index_check()` utility function to check if an index is consistent with its base relation. Use it to detect inconsistencies that can creep in due to faulty storage, faulty RAM, or data files being overwritten or modified by unrelated software.

{{<lead link="../../../api/ysql/exprs/func_yb_index_check/">}}
To understand how to use the function, see [yb_index_check()](../../../api/ysql/exprs/func_yb_index_check/)
{{</lead>}}

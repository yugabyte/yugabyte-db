---
title: SQL features
headerTitle: SQL features
linkTitle: SQL features
description: Explore core SQL features in YSQL
headcontent: Explore core SQL features in YSQL
menu:
  v2025.1:
    identifier: explore-ysql-language-features
    parent: explore
    weight: 100
type: indexpage
showRightNav: true
---
YugabyteDB's [YSQL API](../../api/ysql/) reuses a fork of the [query layer](../../architecture/query-layer/) of PostgreSQL and hence is fully compatible with PostgreSQL. This architecture allows YSQL to support most PostgreSQL features, such as data types, queries, expressions, operators and functions, stored procedures, triggers, extensions, and so on, all of which are expected to work identically on both database systems.

This section provides an overview of the SQL features supported by YugabyteDB, along with details on any limitations or deviations, helping you understand how to leverage YugabyteDB's capabilities while maintaining compatibility with your existing SQL-based workflows.

{{< tip title="Tip" >}}
A large portion of the documentation and examples written for PostgreSQL would work against YSQL.
{{< /tip >}}

## Schemas and tables

A database is a collection of related data, organized into tables, which store the actual records. Schemas act as organizational containers within a database, grouping tables, views, and other objects together to help manage data efficiently. Understanding how these elements work together is essential for designing and managing a well-structured database system.

{{<lead link="databases-schemas-tables/">}}
To learn more, see [Schemas and Tables](databases-schemas-tables/)
{{</lead>}}

## Data types

YugabyteDB offers extensive support for SQL data types, closely aligning with PostgreSQL's type system. It includes common data types like INTEGER, VARCHAR, BOOLEAN, TIMESTAMP, and more, along with advanced types such as arrays and JSONB, similar to PostgreSQL. Understanding the supported data types is crucial for effective schema design and query optimization in YugabyteDB.

{{<lead link="data-types/">}}
To learn more, see [Data types](data-types/)
{{</lead>}}

## Read data

YugabyteDB provides robust support for Data Manipulation Language (DML) statements, which are used to query data in a database. These include standard SQL commands like SELECT, FROM, GROUP BY, and HAVING. YugabyteDB's DML capabilities align closely with PostgreSQL, allowing for complex queries in a distributed environment. Understanding these DML features is crucial for developers and database users to effectively interact with and manipulate data in YugabyteDB's distributed environment.

{{<lead link="queries/">}}
To learn more, see [Read data](queries/)
{{</lead>}}

## Write data

YugabyteDB provides robust support for Data Manipulation Language (DML) statements, which are used to modify data in a database. These include standard SQL commands like INSERT, UPDATE, and DELETE. YugabyteDB's DML capabilities align closely with PostgreSQL, allowing for complex queries, transactions, and data modifications in a distributed environment. Understanding these DML features is crucial for developers and database users to effectively interact with and manipulate data in YugabyteDB's distributed environment.

{{<lead link="data-manipulation/">}}
To learn more, see [Write data](data-manipulation/)
{{</lead>}}

## Expressions and operators

Leveraging its PostgreSQL compatibility, YugabyteDB supports a wide variety of operators—arithmetic, comparison, logical, and string operators—along with complex expressions that allow for powerful data manipulation and retrieval. Understanding these elements is crucial for constructing efficient queries and leveraging the full power of YugabyteDB's SQL interface.

{{<lead link="expressions-operators/">}}
To learn more, see [Expressions and operators](expressions-operators/)
{{</lead>}}

## Data definition - DDL

YugabyteDB supports a broad range of Data Definition Language (DDL) statements, enabling you to create, modify, and manage database objects like tables, indexes, schemas, and more. With a foundation based on PostgreSQL's syntax, YugabyteDB provides compatibility with many standard SQL DDL commands, including CREATE, ALTER, and DROP. Understanding these DDL features is essential for database administrators and developers working with YugabyteDB's schema management and object creation processes

{{<lead link="../../api/ysql/the-sql-language/statements/#data-definition-language-ddl">}}
To learn more, see [Data definition language](../../api/ysql/the-sql-language/statements/#data-definition-language-ddl)
{{</lead>}}

## Indexes

Indexes in YugabyteDB are powerful tools designed to enhance query performance by allowing faster data retrieval. They work by creating a data structure that provides quick access to rows in a table based on the values of one or more columns. YugabyteDB supports various types of indexes, including primary, unique, and secondary indexes, each serving different use cases. By leveraging these indexes, you can significantly reduce the time complexity of read operations, making your applications more efficient and responsive.

{{<lead link="indexes-constraints/">}}
To learn more, see [Indexes](indexes-constraints/)
{{</lead>}}

## Constraints

YugabyteDB supports constraints that enforce rules on the data to ensure accuracy and consistency, such as preventing duplicate values or maintaining relationships between tables. Constraints ensure data validity and integrity, preventing invalid data from being inserted or updated.

{{<lead link="data-manipulation/#constraints">}}
To learn more, see [Constraints](data-manipulation/#constraints)
{{</lead>}}

## Extensions

YugabyteDB offers robust support for PostgreSQL extensions, enabling developers to leverage a wide array of powerful tools and functionalities that enhance database capabilities. By integrating popular PostgreSQL extensions, YugabyteDB ensures compatibility and extends its feature set, allowing you to use familiar tools for tasks such as full-text search, data encryption, and advanced indexing.

{{<lead link="pg-extensions/">}}
To learn more, see [Extensions](pg-extensions/)
{{</lead>}}

## Advanced SQL features

Apart from the standard SQL features, YugabyteDb also supports advanced features like cursors, views, savepoints, triggers, stored procedures, and more.

{{<lead link="advanced-features/">}}
To learn more, see [Advanced features](advanced-features/)
{{</lead>}}

## SQL compatibility

YugabyteDB is a distributed SQL database that implements many standard SQL features while introducing some unique capabilities due to its distributed nature. Given the daunting nature of supporting SQL in a distributed environment, not all features are supported.

{{<lead link="../../api/ysql/sql-feature-support/">}}
To learn more, see [SQL feature support](../../api/ysql/sql-feature-support/)
{{</lead>}}

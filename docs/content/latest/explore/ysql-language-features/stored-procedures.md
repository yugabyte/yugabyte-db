---
title: Stored Procedures
linkTitle: Stored Procedures
description: Stored Procedures in YSQL
headcontent: Stored Procedures in YSQL
image: /images/section_icons/index/explore.png
menu:
  latest:
    identifier: explore-ysql-language-features-stored-procedures
    parent: explore-ysql-language-features
    weight: 300
isTocNested: true
showAsideToc: true
---

This section describes how to use stored procedures to perform transactions.

## Overview

While functions are extremely useful, they have a significant drawback, as well: they can't execute transactions. PostgreSQL 11 introduced stored procedures that support transactions, and Yugabyte supports them as well.

## Creating Stored Procedures

Creating a stored procedure in YSQL is a two-step process: you start by creating a procedure via the `CREATE PROCEDURE` statement, and call the procedure using the `CALL` statement.

The `CREATE PROCEDURE` statement has the following syntax:

```sql

```

The `CALL` statement has the following syntax:

```sql

```

## Deleting Stored Procedures

## Using Stored Procedures

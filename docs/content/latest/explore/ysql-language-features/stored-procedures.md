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

## Creating a Stored Procedure

To create a stored procedure in YSQL, use the `CREATE PROCEDURE` statement, which has the following syntax:

```sql
create [or replace] procedure procedure_name(parameter_list)
language plpgsql
as $$
declare
-- variable declaration
begin
-- stored procedure body
end; $$
```

## Invoking a Stored Procedure

To invoke a stored procedure, use the `CALL` statement, which has the following syntax:

```sql
call stored_procedure_name(argument_list);
```

## Deleting a Stored Procedure

To remove a stored procedure, use the `DROP PROCEDURE` statement, which has the following syntax:

```sql
drop procedure [if exists] procedure_name (argument_list)
[cascade | restrict]
```

## Using Stored Procedures

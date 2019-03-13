---
title: YSQL
linkTitle: YSQL
description: YugaByte Structured Query Language (YSQL) [Beta]
summary: Reference for the YSQL API
image: /images/section_icons/api/ysql.png
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
menu:
  latest:
    identifier: api-ysql
    parent: api
    weight: 2900
aliases:
  - /latest/api/ysql/
isTocNested: true
showAsideToc: true
---

## Introduction
YSQL is a distributed SQL API that is compatible with the SQL dialect of PostgreSQL. Currently, the compatibility is with 11.2 version of PostgreSQL. It is best fit for RDBMS workloads that need horizontal write scalability and global data distribution while also using relational modeling features such as JOINs, distributed transactions and referential integrity (such as foreign keys). 

The main components of YSQL are data definition language (DDL), data manipulation language (DML), and data control language (DCL). Several other components are also provided for different purposes such as system control, transaction control, and performance tuning.

A number of elements are used to construct the SQL language in YSQL such as datatypes, database objects, names and qualifiers, expressions, and comments.

The YSQL language is described in detail in the rest of this section, starting with the [overview](overview) page.
To try YSQL yourself, follow our [quick start](../../quick-start/) instructions.

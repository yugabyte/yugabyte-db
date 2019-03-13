---
title: DATE-TIME Datatypes
linktitle: Date-time
summary: DATE-TIME Datatypes
description: DATE-TIME Datatypes
menu:
  latest:
    identifier: api-ysql-datatypes-datetime
    parent: api-ysql-datatypes
aliases:
  - /latest/api/ysql/datatypes/type_datetime
isTocNested: true
showAsideToc: true
---

## Synopsis
DATE, TIME, TIMESTAMP, and INTERVAL datatypes are supported in YSQL.

Datatype | Description | Min | Max |
---------|-------------|-----|-----|
TIMESTAMP [ (p) ] [ WITHOUT TIME ZONE ] | 8-byte date and time | 4713 BC | 294276 AD |
TIMESTAMP [ (p) ] WITH TIME ZONE | 8-byte date and time | 4713 BC | 294276 AD |
DATE | 4-byte date | 4713 BC | 5874897 AD |
TIME [ (p) ] [ WITHOUT TIME ZONE ] | 8-byte time of day | 00:00:00 | 24:00:00 |
TIME [ (p) ] WITH TIME ZONE | 12-byte time of day | 00:00:00+1459 | 24:00:00-1459 |
INTERVAL [ fields ] [ (p) ] | 16-byte time interval | -178000000 years | 178000000 years |

## Description

- Date and time inputs can be in various format including ISO, SQL, Postgres-extension, and many others.

## See Also

[Data Types](../datatypes)

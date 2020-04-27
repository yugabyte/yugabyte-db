---
title: Programmatic construction of the literal for an array of row type valuess
linkTitle: Programmatic literal construction
headerTitle: Programmatic construction of the literal for an array of row type values
description: Bla bla
menu:
  latest:
    identifier: programmatic-literal-construction
    parent: array-literals
    weight: 70
isTocNested: false
showAsideToc: false
---

Create the _"row"_ type that was used to illustrate the *"[Example use case: GPS trip data](../#example-use-case-gps-trip-data)"* section:

```postgresql
create type gps_data_point_t as (
  ts          timestamp,
  lat         numeric,
  long        numeric,
  alt         numeric,
  cadence     int,
  heart_rate  int);
```
Now create a simple target table into which a you can insert a value of this data type:

```postgresql
create table t(
  k serial primary key,
  gps_data_points  gps_data_point_t[]);
```

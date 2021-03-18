---
title: Tablespaces
linkTitle: Tablespaces
description: Tablespaces in YSQL
headcontent: Tablespaces in YSQL
image: /images/section_icons/secure/create-roles.png
menu:
  latest:
    identifier: explore-ysql-language-features-tablespaces
    parent: explore-ysql-language-features
    weight: 300
isTocNested: true
showAsideToc: true
---

This document describes how use tablespaces in YSQL.

## Overview

YSQL tablespaces are entities that specify how data associated with them should be replicated and distributed across cloud, regions, and timezones.

In PostgreSQL, tablespaces are used for specifying a location on disk with options to control data access. That is, tablespaces group tables and indexes based on how you intend to store and access the data. For instance, you can choose to place heavily accessed smaller tables and indexes in SSD or “fast” storage compared to other tables.

For YugabyteDB clusters, however, location does not pertain to disk locations. For a cloud-native distributed database, location pertains to the cloud, regions, and timezone where the data is supposed to be. Therefore, although YSQL tablespaces are built on PostgreSQL tablespaces that allow you to specify placement of data at a table level, this placement information in YSQL defines the number of replicas for a table and index, as well as how they can be distributed across a set of cloud, regions, and timezones. 

## Defining a Tablespace

You can define a tablespace using the following syntax:

```sql
CREATE TABLESPACE tablespace_name 
  OWNER user_name
  LOCATION 'ignored_value'
  WITH ( replica_placement = placement_policy_json );
```

*tablespace_name* represents the name of the tablespace to be created. *user_name* represents the name of the user who will own the tablespace, with the name of the user executing the command being the default. Note that only superusers can create tablespaces and grant their ownership to other types of users. *placement_policy_json* represents a JSON string that specifies the placement policy for this tablespace. The JSON structure contains two fields: *num_replicas* that defines the overall replication factor, and *placement_blocks* which is an array of tuples, with each tuple containing the keys `<”cloud”, “region”, “zone”, and “min_num_replicas”>` whose values define a placement block. Typically the sum of *min_num_replicas* across all placement blocks is expected to be equal to `num_replicas`. The aggregate of `min_num_replicas` can be lesser than `num_replicas`, in which case the extra replicas are placed at the YB-Load balancer’s discretion. *ignored_value* represents the `LOCATION` field value that is discarded because it is not applicable to YSQL, yet it is accepted in order to maintain parity with PostgreSQL. If you do not specify `replica_placement` for a tablespace, then associating the tablespace with a table or index affects how the data of the table is placed.       

## Creating Tables and Indexes in Tablespaces

You can associate new tables and indexes with a corresponding tablespace. This defines the replication factor of the table or index. It also defines how the replicas of the table or index are to be spread across cloud, regions, and timezones.

You can use the YSQL's `TABLESPACE` option with `CREATE TABLE` and `CREATE INDEX` statements, as shown in the following examples:

```sql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  department text,
  change_date date
)
TABLESPACE us_west_tablespace;
```

```sql
CREATE INDEX employee_no_idx ON employees(employee_no) 
TABLESPACE us_east_tablespace;
```

The preceding statements ensure that data in the `employees` and `employee_no_idx` tables is present based on the placement policies specified for `us_west_tablespace` and `us_east_tablespace` respectively.

## Dropping Tablespaces

You can drop a tablespace in YSQL using the following syntax:

```sql
DROP TABLESPACE tablespace_name;
```

The preceding operation cannot be performed if there the tablespace has associated tables or indexes.
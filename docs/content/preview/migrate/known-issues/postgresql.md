---
title: PostgreSQL
linkTitle: PostgreSQL
headcontent: Known issues when migrating data from PostgreSQL.
description: Refer to the known issues when migrating data using YugabyteDB Voyager and suggested workarounds.
menu:
  preview:
    identifier: postgresql-issues
    parent: known-issues
    weight: 102
type: docs
rightNav:
  hideH3: true
---

This page documents known issues you may encounter and suggested workarounds when migrating data from PostgreSQL to YugabyteDB.

## Contents

- [Adding primary key to a partitioned table results in an error](#adding-primary-key-to-a-partitioned-table-results-in-an-error)

### Adding primary key to a partitioned table results in an error

**GitHub**: [Issue #612](https://github.com/yugabyte/yb-voyager/issues/612)

**Description**: If you have a partitioned table in which primary key is added later using `ALTER TABLE`, then the table creation fails with the following error:

```output
ERROR: adding primary key to a partitioned table is not yet implemented (SQLSTATE XX000)
```

**Workaround**: Manual intervention needed. Add primary key in the `CREATE TABLE` statement.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE public.sales_region (
    id integer NOT NULL,
    amount integer,
    branch text,
    region text NOT NULL
)
PARTITION BY LIST (region);

ALTER TABLE ONLY public.sales_region ADD CONSTRAINT sales_region_pkey PRIMARY KEY (id, region);
```

Suggested change to the schema is as follows:

```sql
CREATE TABLE public.sales_region (
    id integer NOT NULL,
    amount integer,
    branch text,
    region text NOT NULL,
    PRIMARY KEY(id, region)
)
PARTITION BY LIST (region);
```

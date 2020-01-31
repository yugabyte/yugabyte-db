# Table Level Partitioning

> **Note:** This is a new feature that is still in a design phase.

PostgreSQL supports table partitioning to split what is logically a large table into smaller physical pieces. This design document outlines how YugabyteDB, a distributed SQL DB, will support this feature. This feature is also used as the building block for **row level geo-partitioning** of tables.

## Motivation

Partitioning can provide several benefits such as:

* **Improve query performance:** In some cases, most of the heavily accessed rows of a table could fall in a single partition or a small number of partitions. Since the indexes are also split into the corresponding partitions, the hot working set of the data blocks can fit in memory improving performance. 

* **Easier to scan one partition than use table-wide index:** Queries or updates access a large percentage of a single partition can be improved by taking advantage of sequential scan of a smaller subset of data in that partition instead of using an index and random access reads scattered across the whole table.

* **Efficient bulk loads and deletes:** These can be accomplished by adding or removing partitions. Detaching or dropping a  partition is much more efficient than a bulk delete operation.

* **Row level geo-partitioning:** List partitioning can be used to place different subsets of data in different geographic locations. This is a powerful feature that allows a logical table to be spread across different regions, each of which can be accessed with low latency from the respective geographies.

# Usage

## Creating partitions

The following forms of partitions will be supported:

### Range Partitioning
The table is partitioned into “ranges” defined by a key column or set of columns, with no overlap between the ranges of values assigned to different partitions.

Example: partitioning by date ranges, an example of partitioning by month is shown below.

1. Create the table with the `PARTITION BY` clause as shown below:
```sql
CREATE TABLE measurement (
    city_id         int not null PRIMARY KEY,
    logdate         date not null,
    peaktemp        int,
    unitsales       int
) PARTITION BY RANGE (logdate);
```

2. Create the desired partitions.

```sql
CREATE TABLE measurement_y2006m02 PARTITION OF measurement
    FOR VALUES FROM ('2020-01-01') TO ('2020-02-01');
    
CREATE TABLE measurement_y2006m03 PARTITION OF measurement
    FOR VALUES FROM ('2020-02-01') TO ('2020-03-01');
    
...
```

### List Partitioning
The table is partitioned by explicitly listing which key values appear in each partition. 

### Hash Partitioning
The table is partitioned by specifying a modulus and a remainder for each partition. Each partition will hold the rows for which the hash value of the partition key divided by the specified modulus will produce the specified remainder.


# Design

## Mapping partitions to DocDB tables

### Adding and removing partitions

### Hash and range sharding

### Colocated tables

### Changing schema

## Queries on partitioned tables

## Support for indexes and constraints

## Support for stored procedures, triggers, functions


# Limitations

Most of the following limitations are very similar to PostgreSQL:

* There is no way to create an exclusion constraint spanning all partitions; it is only possible to constrain each leaf partition individually.
* Unique constraints on partitioned tables must include all the partition key columns.
* While primary keys are supported on partitioned tables, foreign keys referencing partitioned tables are not supported. (Foreign key references from a partitioned table to some other table are supported.)


# Future Work

* Support for row level geo-partitioning

# References

* The excellent [PostgreSQL partitioning documentation](https://www.postgresql.org/docs/11/ddl-partitioning.html).

[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/design/ysql-table-level-partitioning.md?pixel&useReferer)](https://github.com/yugabyte/ga-beacon)

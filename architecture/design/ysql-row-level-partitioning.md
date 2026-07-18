Tracking GitHub Issue: [1958](https://github.com/yugabyte/yugabyte-db/issues/1958)

# YSQL Table Partitioning

PostgreSQL supports table partitioning to split what is logically a large table into smaller physical pieces. This design document outlines how YugabyteDB, a distributed SQL DB, will support this feature. This feature is also used as the building block for **row level geo-partitioning** of tables, where the physical placement of rows can be controlled based on the partitions they fall into.

## Motivation

Partitioning can provide several benefits such as:

* **Improve query performance:** In some cases, most of the heavily accessed rows of a table could fall in a single partition or a small number of partitions. Since the indexes are also split into the corresponding partitions, the hot working set of the data blocks can fit in memory improving performance. 

* **Easier to scan one partition than use table-wide index:** Queries or updates that access a large percentage of a single partition can be improved by taking advantage of sequential scan of a smaller subset of data in that partition instead of using an index and random access reads scattered across the whole table.

* **Efficient bulk loads and deletes:** These can be accomplished by adding or removing partitions. Detaching or dropping a  partition is much more efficient than a bulk delete operation.

* **Row level geo-partitioning:** List partitioning can be used to place different subsets of data in different geographic locations. This is a powerful feature that allows a logical table to be spread across different regions, each of which can be accessed with low latency from the respective geographies. This enables implementation of architectures that can achieve regulatory compliance (such as GDPR) and tiering of data (moving colder data to cheaper tiers).

# Usage

## Types of partitioning

The following forms of partitions are supported today:

### Range Partitioning
The table is partitioned into “ranges” defined by a key column or set of columns, with no overlap between the ranges of values assigned to different partitions.

Example: partitioning by date ranges, an example of partitioning by month is shown below.

1. Create the table with the `PARTITION BY` clause as shown below:
```sql
CREATE TABLE measurement (
    city_id         int not null,
    logdate         date not null,
    peaktemp        int,
    unitsales       int
) PARTITION BY RANGE (logdate);
```

2. Create the desired partitions.

```sql
CREATE TABLE measurement_y2020m02 PARTITION OF measurement
    FOR VALUES FROM ('2020-01-01') TO ('2020-02-01');
    
CREATE TABLE measurement_y2020m03 PARTITION OF measurement
    FOR VALUES FROM ('2020-02-01') TO ('2020-03-01');
    
...
```

### List Partitioning
The table is partitioned by explicitly listing which key values appear in each partition. 

Example: partitioning a table containing information about people by region.

1. Table creation:
```sql
CREATE TABLE person (
    person_id         int not null,
    country           text
) PARTITION BY LIST (country);
```

2. Create the desired partitions.

```sql
CREATE TABLE person_americas PARTITION OF person
    FOR VALUES IN ('United States', 'Brazil', 'Mexico', 'Columbia');
    
CREATE TABLE person_APAC PARTITION OF Person
    FOR VALUES IN ('India', 'Sri Lanka', 'Singapore', 'Japan', 'China');    
...
```

### Hash Partitioning
The table is partitioned by specifying a modulus and a remainder for each partition. Each partition will hold the rows for which the hash value of the partition key divided by the specified modulus will produce the specified remainder. This can be used to approximately evenly split the rows of a table into the specified number of partitions.

1. Create the table with the `PARTITION BY` clause as shown below:
```sql
CREATE TABLE department (
    id  int primary key
) PARTITION BY hash(id) ; 

```

2. Create the desired partitions.

```sql
CREATE TABLE department_1 partition of department FOR VALUES WITH (MODULUS 3, REMAINDER 0);
CREATE TABLE department_2 partition of department FOR VALUES WITH (MODULUS 3, REMAINDER 1);
CREATE TABLE department_3 partition of department FOR VALUES WITH (MODULUS 3, REMAINDER 2);
    
...
```

## Inserting into partitioned tables
Rows can directly be inserted into the child partition or into the parent partitioned table. When inserted into the parent partitioned table, the row is inserted into the appropriate child partition based on the partition key as shown in the following example:

1. Create partitioned table
```sql
CREATE TABLE person (                           
     person_id         int,                                                                         
     country           text
)PARTITION BY LIST (country);
```

2. Create partitions
```sql
CREATE TABLE person_apac PARTITION OF person 
    FOR VALUES IN ('India', 'Singapore', 'Japan', 'China', 'Malaysia');
CREATE TABLE person_americas PARTITION OF person
    FOR VALUES IN ('United States', 'Mexico', 'Canada', 'Argentina', 'Brazil');
```

3. Insert into parent partitioned table:
```sql
INSERT INTO person VALUES (1, 'United States');
INSERT 0 1
```

4. Insert directly into child table:
```sql
INSERT INTO person_apac VALUES (2, 'India');
INSERT 0 1
```

> **Note:** Inserting a row into the parent partitioned table that does not satisfy the partition constraint for any of its child partitions will fail. To allow insertion of rows that do not satisfy partition constraints of any explicitly created partitions, check the next section on default partitions.

5. Select from all the tables. The entry inserted into the partitioned table has been routed to the appropriate partition, and the entry directly inserted into the child table has also been inserted into the child table. Both rows are accessible by querying the parent table.

```sql
SELECT tableoid::regclass, * FROM person;
    tableoid     | person_id |    country    
-----------------+-----------+---------------
 person_americas |         1 | United States
 person_apac     |         2 | India
(2 rows)
```

Update of an existing row can sometimes cause the row to not satisfy the partition constraint of the child partition in which it currently exists. Under the hood, YSQL performs a transaction to DELETE the row from the current partition, and INSERT the row into the new partition that it belongs to. Thus an UPDATE operation on one child partition table can trigger the row level DELETE triggers associated with it in addition to row level INSERT triggers on some other child partition table.

## Default Partitions
If a partitioned table has a DEFAULT partition, then a partition key value that does not have a corresponding partition to be routed to, will be routed to the DEFAULT partition; for example, in the case where the table is list partitioned, all the rows which do not fit in any of the lists for given partitions, would go to the DEFAULT partition.

1. Create table and associated partitions:
```sql
CREATE TABLE list_parted(a int, b int) PARTITION BY LIST(a);
CREATE TABLE list_part_1 PARTITION OF list_parted FOR VALUES IN (1, 2, 3);
CREATE TABLE list_part_2 PARTITION OF list_parted FOR VALUES IN (6, 7, 8);
```

2. Create default partition
```sql
CREATE TABLE list_part_default PARTITION OF list_parted DEFAULT;
```

3. Insert rows which have keys that do not fit into any specified partitions:
```sql
INSERT INTO list_parted VALUES (1, 11), (4, 44), (7, 77), (9, 99);
INSERT 0 4 
```

4. Rows not having a specified partition will be routed to the default partition:
```sql
yugabyte=# SELECT tableoid::regclass, * FROM list_parted;
    tableoid      | a | b
-------------------+---+----
list_part_1       | 1 | 11
list_part_2       | 7 | 77
list_part_default | 4 | 44
list_part_default | 9 | 99
(3 rows)
```

## Attach and Detach Partitions
YSQL also supports attaching any existing table to a partitioned table as one of its partitions, given the existing table satisfies the partition constraints. This allows the data to be loaded, checked, and transformed prior to it appearing in the partitioned table. Creating a constraint on this table as shown below can help avoid the scan performed later while attaching the partition to ensure that data within this table satisfies the partition constraint. This constraint can be dropped later after the table has been attached as a partition.

```sql
CREATE TABLE measurement_y2020m06 (
    city_id         int not null PRIMARY KEY,
    logdate         date not null,
    peaktemp        int,
    unitsales       int
);

ALTER TABLE measurement_y2020m06 ADD CONSTRAINT y2020m06
   CHECK ( logdate >= DATE '2020-06-01' AND logdate < DATE '2020-07-01' );

-- possibly some other data preparation work

ALTER TABLE measurement ATTACH PARTITION measurement_y2020m06
    FOR VALUES FROM ('2020-06-01') TO ('2020-07-01' );
```

Another common use-case is to remove partitions periodically and add new partitions. Detaching a partition and discarding huge amounts of data can be done efficiently with a partitioned table as this merely manipulates the partition structure.

The simplest option for removing old data is to drop the partition that is no longer necessary:

```sql
DROP TABLE measurement_y2020m02;
```

Another option is to retain access to the table, but remove it from the partitioned hierarchy:

```sql
ALTER TABLE measurement DETACH PARTITION measurement_y2020m02;
```

## Sub-partitions
A child partition of a partitioned table can be further sub-partitioned as shown below:

1. Create parent partitioned table:
```sql
CREATE TABLE person (
    person_id         int,
    country           text
) PARTITION BY LIST (country);
```

2. Create partition for the parent partitioned table, that is also a partitioned table.
```sql
CREATE TABLE person_americas PARTITION OF person
    FOR VALUES IN ('United States', 'Brazil', 'Mexico', 'Columbia', 'Canada', 'Cuba', 'Argentina')
    PARTITIONED BY(country);
```

3. Create sub-partitions for the child partition created above.
```sql
CREATE TABLE person_NorthAmerica PARTITION OF person_americas
    FOR VALUES IN ('United States', 'Mexico', 'Canada');
CREATE TABLE person_SouthAmerica PARTITION OF person_americas
    FOR VALUES IN ('Brazil', 'Columbia', 'Cuba', 'Argentina');
```

# Design

## Implementation
When parent partitioned tables or child partition tables are created, they are very similar entities in YSQL to regular tables. In fact most operations supported for non-partitioned tables are also supported for partitioned tables. Hence both parent and child tables are backed by DocDB tables much like non-partitioned tables. However, the following additional metadata is stored in the sys catalog tables for parent partitioned and child partition tables:

1) pg_class: The table has an entry depicting whether or not it is partitioned. Another entry depicts whether this table is a child partition. If it is a child partition, its partition bound is represented in its pg_class entry.

2) pg_inherits: There is one entry in pg_inherits for each direct parent-child table relationship in the database.

3) pg_partitioned_table: This table contains an entry for every partitioned table. It contains information about the partitioned table, such as partitioning strategy, partition keys etc.

Both select and update queries directed to parent partitioned tables are routed to their appropriate partitions by the YSQL layer itself and do not need any additional functionality from DocDB. Each DocDB table backing a child partition can be sharded into tablets like regular non-partitioned tables. The parent partitioned table also has a corresponding table in DocDB, although in practice, it would not have any rows associated with it.

### Adding and removing partitions
Attaching and detaching partitions are metadata updates in YSQL, hence this also does not require any additional functionality from DocDB.

### Hash and range sharding
Tables created using YSQL can be hash or range sharded, the same holds true for partitioned tables. However, ideally child partition tables must inherit the sharding strategy as that of the parent. However, since today DocDB is not aware of table inheritance, the YSQL layer will need to send the table_id of the parent partition table for the sharding strategy to be passed down to the child partition table.

### Colocated tables
Databases in YSQL can have tables that are backed by a single tablet in DocDB. As with sharding strategies above, specifying that a parent partitioned table is backed by colocated tablet should ensure that the same setting is passed down to the child tables as well. As with sharding strategies, propagation of colocation attributes from parent to child tables must also be handled by DocDB.

### Changing table schema
ALTER table queries currently supported by Yugabyte will also work with partitioned tables. In addition, as shown above, ATTACH PARTITION and DETACH PARTITION statements can also be supported easily as these are merely metadata updates at the YSQL layer and do not involve any additional support from DocDB.

> **Note:** Need to investigate the transaction processing involved for altering a parent partitioned table and subsequent  propagation of the altered schema to its child partitions.

## Queries on partitioned tables

### Insert:
Insertions into child partitions directly are straightforward and not very different from regular insertions. However, rows inserted into the parent partitioned table are routed to the appropriate partition by the YSQL executor. This eventually translates into an insert request on the appropriate child table to DocDB. DocDB will never receive insert requests to the parent partitioned table.

### Select
When select queries arrive for the parent partitioned table, the planner identifies that this is a partitioned table, and appropriately translates them into operations on the child partition tables. The executor evaluates the partition constraints and determines which partitions are valid for the current query, and prunes the partitions that are not necessary to be queried. Again, DocDB will not receive any queries for the parent partitioned table itself, and will only need to handle queries on the leaf partition tables themselves.

### Support for indexes, constraints and triggers
In general, constraints, indices and triggers applied to a parent partitioned table are inherited by all the child partitions, and on any child partition later created or attached. These can also be created for a specific partition by directly creating them on the child partition table itself. 

Additionally, when indices are created on the partitioned table, it does not have any data associated with it, and will manifest only as individual indices on the child partitions. There is no global index to maintain order across all partitions. 

Also, with triggers, modifying a partitioned table fires statement-level triggers attached to the explicitly named table, but not statement-level triggers for the child tables that were also affected by the statement.
In contrast, row-level triggers are fired on the rows in affected partitions or child tables, even if they are not explicitly named in the query.

# Current State of Implementation
The current implementation enables partitions, fetches PostgreSQL partitioning test suites and has some fixes to ensure that partitioning feature works as expected. 

### Items under investigation
* When creating partitioned tables with foreign keys referencing a non-partitioned table, setting ON DELETE and ON DELETE CASCADE actions do not work correctly
* AFTER UPDATE triggers do not fire correctly on partitioned tables
* Investigate making the parent partitioned table a pure metadata entity, instead of creating a DocDB table for the parent partitioned table.
* While creating tables in DocDB for child partitions, the YSQL layer will have to push down the parent information to DocDB as well. Thus at child partition creation time, DocDB will be able to apply the sharding strategies and colocation settings of the parent partitioned table to the child table. 
* Evaluate merging latest improvements to the partitioning feature in PostgreSQL 12 (https://www.2ndquadrant.com/en/blog/postgresql-12-partitioning/)


# Limitations

Most of the following limitations are very similar to PostgreSQL:
* There is no way to create an exclusion constraint spanning all partitions; it is only possible to constrain each leaf partition individually.
* Unique constraints on partitioned tables must include all the partition key columns.
* While primary keys are supported on partitioned tables, foreign keys referencing partitioned tables are not supported. (Foreign key references from a partitioned table to some other non-partitioned table are supported.)

# Future Work

* Support for row level geo-partitioning: This involves piggy-backing on LIST partitioning. The partition key will contain the name of the region to which the row must be routed to. Thus all rows associated with a region will fall into the same partition (i.e. a single table on DocDB). This value will be used by DocDB to place the tablets of such tables in the specified regions.

# References

* The excellent [PostgreSQL partitioning documentation](https://www.postgresql.org/docs/11/ddl-partitioning.html).

[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/design/ysql-table-level-partitioning.md?pixel&useReferer)](https://github.com/yugabyte/ga-beacon)

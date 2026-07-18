Example Guide On Setting Up Native Partitioning
========================================

- [Simple Time Based: 1 Partition Per Day](#simple-time-based-1-partition-per-day)
- [Simple Serial ID: 1 Partition Per 10 ID Values](#simple-serial-id-1-partition-Per-10-id-values)
- [Partitioning an Existing Table](#partitioning-an-existing-table)
  * [Offline Partitioning](#offline-partitioning)
  * [Online Partitioning](#online-partitioning)
- [Undoing Native Partitioning](#undoing-native-partitioning)

This HowTo guide will show you some examples of how to set up simple, single level partitioning. It will also show you several methods to partition data out of a table that has existing data (see [Partitioning an Existing Table](#partitioning-an-existing-table)) and undo the partitioning of an existing partition set (see [Undoing Native Partitioning](#undoing-native-partitioning)). For more details on what each function does and the additional features in this extension, please see the **pg_partman.md** documentation file.

The examples in this document assume you are running at least 4.4.1 of pg_partman with PostgreSQL 11 or higher. 

Note that all examples here are for native partitioning. If you need to use non-native, trigger-based partitioning, please see the [Trigger-based HowTo](pg_partman_howto_triggerbased.md) file.

### Simple Time Based: 1 Partition Per Day
For native partitioning, you must start with a parent table that has already been set up to be partitioned in the desired type. Currently pg_partman only supports the RANGE type of partitioning (both for time & id). You cannot turn a non-partitioned table into the parent table of a partitioned set, which can make migration a challenge. This document will show you some techniques for how to manage this later. For now, we will start with a brand new table for this example. Any non-unique indexes can also be added to the parent table in PG11+ and they will automatically be created on all child tables.

```
CREATE SCHEMA IF NOT EXISTS partman_test;

CREATE TABLE partman_test.time_taptest_table 
    (col1 int, 
    col2 text default 'stuff', 
    col3 timestamptz NOT NULL DEFAULT now()) 
PARTITION BY RANGE (col3);

CREATE INDEX ON partman_test.time_taptest_table (col3);
```
```
\d+ partman_test.time_taptest_table 
                               Partitioned table "partman_test.time_taptest_table"
 Column |           Type           | Collation | Nullable |    Default    | Storage  | Stats target | Description 
--------+--------------------------+-----------+----------+---------------+----------+--------------+-------------
 col1   | integer                  |           |          |               | plain    |              | 
 col2   | text                     |           |          | 'stuff'::text | extended |              | 
 col3   | timestamp with time zone |           | not null | now()         | plain    |              | 
Partition key: RANGE (col3)
Indexes:
    "time_taptest_table_col3_idx" btree (col3)
Number of partitions: 0
```

Unique indexes (including primary keys) cannot be created on a natively partitioned parent unless they include the partition key. For time-based partitioning that generally doesn't work out since that would limit only a single timestamp value in each child table. pg_partman helps to manage this by using a template table to manage properties that currently are not supported by native partitioning. Note that this does *not* solve the issue of the constraint *not* being enforced across the entire partition set. See the [main documentation](pg_partman.md#child-table-property-inheritance) to see which properties are managed by the template, depending on the version of PostgreSQL.

For this example, we are going to manually create the template table first so that when we run `create_parent()` the initial child tables that are created will have a primary key. If you do not supply a template table to pg_partman, it will create one for you in the schema that you installed the extension to. However properties you add to that template are only then applied to newly created child tables after that point. You will have to retroactively apply those properties manually to any child tables that already existed.
```
CREATE TABLE partman_test.time_taptest_table_template (LIKE partman_test.time_taptest_table);
ALTER TABLE partman_test.time_taptest_table_template ADD PRIMARY KEY (col1);
```
```
 \d partman_test.time_taptest_table_template
          Table "partman_test.time_taptest_table_template"
 Column |           Type           | Collation | Nullable | Default 
--------+--------------------------+-----------+----------+---------
 col1   | integer                  |           | not null | 
 col2   | text                     |           |          | 
 col3   | timestamp with time zone |           | not null | 
Indexes:
    "time_taptest_table_template_pkey" PRIMARY KEY, btree (col1)
```
```
SELECT partman.create_parent('partman_test.time_taptest_table', 'col3', 'native', 'daily', p_template_table := 'partman_test.time_taptest_table_template');
 create_parent 
---------------
 t
(1 row)
```
```
\d+ partman_test.time_taptest_table
                               Partitioned table "partman_test.time_taptest_table"
 Column |           Type           | Collation | Nullable |    Default    | Storage  | Stats target | Description 
--------+--------------------------+-----------+----------+---------------+----------+--------------+-------------
 col1   | integer                  |           |          |               | plain    |              | 
 col2   | text                     |           |          | 'stuff'::text | extended |              | 
 col3   | timestamp with time zone |           | not null | now()         | plain    |              | 
Partition key: RANGE (col3)
Indexes:
    "time_taptest_table_col3_idx" btree (col3)
Partitions: partman_test.time_taptest_table_p2020_10_26 FOR VALUES FROM ('2020-10-26 00:00:00-04') TO ('2020-10-27 00:00:00-04'),
            partman_test.time_taptest_table_p2020_10_27 FOR VALUES FROM ('2020-10-27 00:00:00-04') TO ('2020-10-28 00:00:00-04'),
            partman_test.time_taptest_table_p2020_10_28 FOR VALUES FROM ('2020-10-28 00:00:00-04') TO ('2020-10-29 00:00:00-04'),
            partman_test.time_taptest_table_p2020_10_29 FOR VALUES FROM ('2020-10-29 00:00:00-04') TO ('2020-10-30 00:00:00-04'),
            partman_test.time_taptest_table_p2020_10_30 FOR VALUES FROM ('2020-10-30 00:00:00-04') TO ('2020-10-31 00:00:00-04'),
            partman_test.time_taptest_table_p2020_10_31 FOR VALUES FROM ('2020-10-31 00:00:00-04') TO ('2020-11-01 00:00:00-04'),
            partman_test.time_taptest_table_p2020_11_01 FOR VALUES FROM ('2020-11-01 00:00:00-04') TO ('2020-11-02 00:00:00-05'),
            partman_test.time_taptest_table_p2020_11_02 FOR VALUES FROM ('2020-11-02 00:00:00-05') TO ('2020-11-03 00:00:00-05'),
            partman_test.time_taptest_table_p2020_11_03 FOR VALUES FROM ('2020-11-03 00:00:00-05') TO ('2020-11-04 00:00:00-05'),
            partman_test.time_taptest_table_default DEFAULT
```
```
\d+ partman_test.time_taptest_table_p2020_10_26
                               Table "partman_test.time_taptest_table_p2020_10_26"
 Column |           Type           | Collation | Nullable |    Default    | Storage  | Stats target | Description 
--------+--------------------------+-----------+----------+---------------+----------+--------------+-------------
 col1   | integer                  |           | not null |               | plain    |              | 
 col2   | text                     |           |          | 'stuff'::text | extended |              | 
 col3   | timestamp with time zone |           | not null | now()         | plain    |              | 
Partition of: partman_test.time_taptest_table FOR VALUES FROM ('2020-10-26 00:00:00-04') TO ('2020-10-27 00:00:00-04')
Partition constraint: ((col3 IS NOT NULL) AND (col3 >= '2020-10-26 00:00:00-04'::timestamp with time zone) AND (col3 < '2020-10-27 00:00:00-04'::timestamp with time zone))
Indexes:
    "time_taptest_table_p2020_10_26_pkey" PRIMARY KEY, btree (col1)
    "time_taptest_table_p2020_10_26_col3_idx" btree (col3)
Access method: heap

```

### Simple Serial ID: 1 Partition Per 10 ID Values
For this use-case, the template table is not created manually before calling `create_parent()`. So it shows that if a primary/unique key is added later, it does not apply to the currently existing child tables. That will have to be done manually. 

```
CREATE TABLE partman_test.id_taptest_table (
    col1 bigint 
    , col2 text not null
    , col3 timestamptz DEFAULT now()
    , col4 text) PARTITION BY RANGE (col1);

CREATE INDEX ON partman_test.id_taptest_table (col1);
```
```
\d+ partman_test.id_taptest_table 
                             Partitioned table "partman_test.id_taptest_table"
 Column |           Type           | Collation | Nullable | Default | Storage  | Stats target | Description 
--------+--------------------------+-----------+----------+---------+----------+--------------+-------------
 col1   | bigint                   |           |          |         | plain    |              | 
 col2   | text                     |           | not null |         | extended |              | 
 col3   | timestamp with time zone |           |          | now()   | plain    |              | 
 col4   | text                     |           |          |         | extended |              | 
Partition key: RANGE (col1)
Indexes:
    "id_taptest_table_col1_idx" btree (col1)
Number of partitions: 0
```
```

SELECT partman.create_parent('partman_test.id_taptest_table', 'col1', 'native', '10');
 create_parent 
---------------
 t
(1 row)
```
```
\d+ partman_test.id_taptest_table
                             Partitioned table "partman_test.id_taptest_table"
 Column |           Type           | Collation | Nullable | Default | Storage  | Stats target | Description 
--------+--------------------------+-----------+----------+---------+----------+--------------+-------------
 col1   | bigint                   |           |          |         | plain    |              | 
 col2   | text                     |           | not null |         | extended |              | 
 col3   | timestamp with time zone |           |          | now()   | plain    |              | 
 col4   | text                     |           |          |         | extended |              | 
Partition key: RANGE (col1)
Indexes:
    "id_taptest_table_col1_idx" btree (col1)
Partitions: partman_test.id_taptest_table_p0 FOR VALUES FROM ('0') TO ('10'),
            partman_test.id_taptest_table_p10 FOR VALUES FROM ('10') TO ('20'),
            partman_test.id_taptest_table_p20 FOR VALUES FROM ('20') TO ('30'),
            partman_test.id_taptest_table_p30 FOR VALUES FROM ('30') TO ('40'),
            partman_test.id_taptest_table_p40 FOR VALUES FROM ('40') TO ('50'),
            partman_test.id_taptest_table_default DEFAULT
```

You can see the name of the template table by looking in the pg_partman configuration for that parent table

```
select template_table from partman.part_config where parent_table = 'partman_test.id_taptest_table';
                 template_table                 
------------------------------------------------
 partman.template_partman_test_id_taptest_table
```
```
ALTER TABLE partman.template_partman_test_id_taptest_table ADD PRIMARY KEY (col2);
```
Now if we add some data and run maintenance again to create new child tables...
```
INSERT INTO partman_test.id_taptest_table (col1, col2) VALUES (generate_series(1,20), generate_series(1,20)::text||'stuff'::text);

CALL partman.run_maintenance_proc();

\d+ partman_test.id_taptest_table
                             Partitioned table "partman_test.id_taptest_table"
 Column |           Type           | Collation | Nullable | Default | Storage  | Stats target | Description 
--------+--------------------------+-----------+----------+---------+----------+--------------+-------------
 col1   | bigint                   |           |          |         | plain    |              | 
 col2   | text                     |           | not null |         | extended |              | 
 col3   | timestamp with time zone |           |          | now()   | plain    |              | 
 col4   | text                     |           |          |         | extended |              | 
Partition key: RANGE (col1)
Indexes:
    "id_taptest_table_col1_idx" btree (col1)
Partitions: partman_test.id_taptest_table_p0 FOR VALUES FROM ('0') TO ('10'),
            partman_test.id_taptest_table_p10 FOR VALUES FROM ('10') TO ('20'),
            partman_test.id_taptest_table_p20 FOR VALUES FROM ('20') TO ('30'),
            partman_test.id_taptest_table_p30 FOR VALUES FROM ('30') TO ('40'),
            partman_test.id_taptest_table_p40 FOR VALUES FROM ('40') TO ('50'),
            partman_test.id_taptest_table_p50 FOR VALUES FROM ('50') TO ('60'),
            partman_test.id_taptest_table_p60 FOR VALUES FROM ('60') TO ('70'),
            partman_test.id_taptest_table_default DEFAULT
```
... you'll see that only the new child tables (p50 & p60) have that primary key and the original tables do not (p40 and earlier).
```
\d partman_test.id_taptest_table_p40
             Table "partman_test.id_taptest_table_p40"
 Column |           Type           | Collation | Nullable | Default 
--------+--------------------------+-----------+----------+---------
 col1   | bigint                   |           |          | 
 col2   | text                     |           | not null | 
 col3   | timestamp with time zone |           |          | now()
 col4   | text                     |           |          | 
Partition of: partman_test.id_taptest_table FOR VALUES FROM ('40') TO ('50')
Indexes:
    "id_taptest_table_p40_col1_idx" btree (col1)

\d partman_test.id_taptest_table_p50
             Table "partman_test.id_taptest_table_p50"
 Column |           Type           | Collation | Nullable | Default 
--------+--------------------------+-----------+----------+---------
 col1   | bigint                   |           |          | 
 col2   | text                     |           | not null | 
 col3   | timestamp with time zone |           |          | now()
 col4   | text                     |           |          | 
Partition of: partman_test.id_taptest_table FOR VALUES FROM ('50') TO ('60')
Indexes:
    "id_taptest_table_p50_pkey" PRIMARY KEY, btree (col2)
    "id_taptest_table_p50_col1_idx" btree (col1)

\d partman_test.id_taptest_table_p60
             Table "partman_test.id_taptest_table_p60"
 Column |           Type           | Collation | Nullable | Default 
--------+--------------------------+-----------+----------+---------
 col1   | bigint                   |           |          | 
 col2   | text                     |           | not null | 
 col3   | timestamp with time zone |           |          | now()
 col4   | text                     |           |          | 
Partition of: partman_test.id_taptest_table FOR VALUES FROM ('60') TO ('70')
Indexes:
    "id_taptest_table_p60_pkey" PRIMARY KEY, btree (col2)
    "id_taptest_table_p60_col1_idx" btree (col1)
```
Add them manually:
```
ALTER TABLE partman_test.id_taptest_table_p0 ADD PRIMARY KEY (col2);
ALTER TABLE partman_test.id_taptest_table_p10 ADD PRIMARY KEY (col2);
ALTER TABLE partman_test.id_taptest_table_p20 ADD PRIMARY KEY (col2);
ALTER TABLE partman_test.id_taptest_table_p30 ADD PRIMARY KEY (col2);
ALTER TABLE partman_test.id_taptest_table_p40 ADD PRIMARY KEY (col2);
```

### Partitioning an Existing Table

Partitioning an existing table with native partitioning is not as straight forward as methods that could be employed with the old trigger-based methods. As stated above, you cannot turn an already existing table into the parent table of a native partition set. The parent of a native partitioned table must be declared partitioned at the time of its creation. However, there are still methods to take an existing table and partition it natively. Two of those are presented below.

#### Offline Partitioning


This method is being labelled "offline" because, during points in this process, the data is not accessible to both the new and old table from a single object. The data is moved from the original table to a brand new table. The advantage of this method is that you can move your data in much smaller batches than even the target partition size, which can be a huge efficiency advantage for very large partition sets (you can commit in batches of several thousand vs several million). There are also less object renaming steps as we'll see in the online partitioning method next.

*IMPORTANT NOTE REGARDING FOREIGN KEYS*

Taking the partitioned table offline is the only method that realistically works when you have foreign keys TO the table being partitioned. Since a brand new table must be created no matter what, the foreign key must also be recreated, so an outage involving all tables that are part of the FK relationship must be taken. A shorter outage may be possible with the online method below, but if you have to take an outage, this offline method is easier.

Here is the original table with some generated data:

```
CREATE TABLE public.original_table (
    col1 bigint not null
    , col2 text not null
    , col3 timestamptz DEFAULT now()
    , col4 text); 

CREATE INDEX ON public.original_table (col1);

INSERT INTO public.original_table (col1, col2, col3, col4) VALUES (generate_series(1,100000), 'stuff'||generate_series(1,100000), now(), 'stuff');
```

First, the original table should be renamed so the partitioned table can be made with the original table's name. This makes it so that, when the child tables are created, they have names that are associated with the original table name. 
```
ALTER TABLE public.original_table RENAME to old_nonpartitioned_table;
```
We'll use the serial partitioning example from above. The initial setup is exactly the same, creating a brand new table that will be the parent and then running `create_parent()` on it. We'll make the interval slightly larger this time. Also, make sure you've applied all the same original properties to this new table that the old table had: privileges, constraints, defaults, indexes, etc. Privileges are especially important to make sure they match so that all users of the table will continue to work after the conversion.

Note that primary keys/unique indexes cannot be applied to a partitioned parent unless the partition key is part of it. In this case that would work, however it's likely not the intention since that would mean only one row per value is allowed and that would mean only 10,000 rows could ever exist in each child table. Partitioning is definitely not needed then. The next example of online partitioning will show how to handle when you need a primary key for a column that is not part of the partition key.

```
CREATE TABLE public.original_table (
    col1 bigint not null
    , col2 text not null
    , col3 timestamptz DEFAULT now()
    , col4 text) PARTITION BY RANGE (col1);

CREATE INDEX ON public.original_table (col1);

SELECT partman.create_parent('public.original_table', 'col1', 'native', '10000');
```
```
\d+ original_table;
                                 Partitioned table "public.original_table"
 Column |           Type           | Collation | Nullable | Default | Storage  | Stats target | Description 
--------+--------------------------+-----------+----------+---------+----------+--------------+-------------
 col1   | bigint                   |           | not null |         | plain    |              | 
 col2   | text                     |           | not null |         | extended |              | 
 col3   | timestamp with time zone |           |          | now()   | plain    |              | 
 col4   | text                     |           |          |         | extended |              | 
Partition key: RANGE (col1)
Indexes:
    "original_table_col1_idx1" btree (col1)
Partitions: original_table_p0 FOR VALUES FROM ('0') TO ('10000'),
            original_table_p10000 FOR VALUES FROM ('10000') TO ('20000'),
            original_table_p20000 FOR VALUES FROM ('20000') TO ('30000'),
            original_table_p30000 FOR VALUES FROM ('30000') TO ('40000'),
            original_table_p40000 FOR VALUES FROM ('40000') TO ('50000'),
            original_table_default DEFAULT
```
If you happened to be using IDENTITY columns, or you created a new sequence for the new partitioned table, you'll want to get the value of those old sequences and reset the new sequences to start with those old values. Some tips for doing that are covered in the Online Partitioning section below. If you just re-used the same sequence on the new partitioned table, you should be fine.

Now we can use the `partition_data_proc()` procedure to migrate our data from the old table to the new table. And we're going to do it in 1,000 row increments vs the 10,000 interval that the partition set has. The batch value is used to tell it how many times to run through the given interval; the default value of 1 only makes a single child table. Since we want to partition all of the data, just give it a number equal to or greater than the expected child table count. This procedure has an option where you can tell it the source of the data, which is how we're going to migrate the data from the old table. Without setting this option, it attempts to clean the data out of the DEFAULT partition (which we'll see an example of next).

```
CALL partman.partition_data_proc('public.original_table', p_interval := '1000', p_batch := 200, p_source_table := 'public.old_nonpartitioned_table');
NOTICE:  Batch: 1, Rows moved: 1000
NOTICE:  Batch: 2, Rows moved: 1000
NOTICE:  Batch: 3, Rows moved: 1000
NOTICE:  Batch: 4, Rows moved: 1000
NOTICE:  Batch: 5, Rows moved: 1000
NOTICE:  Batch: 6, Rows moved: 1000
NOTICE:  Batch: 7, Rows moved: 1000
NOTICE:  Batch: 8, Rows moved: 1000
NOTICE:  Batch: 9, Rows moved: 1000
NOTICE:  Batch: 10, Rows moved: 999
NOTICE:  Batch: 11, Rows moved: 1000
NOTICE:  Batch: 12, Rows moved: 1000
[...]
NOTICE:  Batch: 100, Rows moved: 1000
NOTICE:  Batch: 101, Rows moved: 1
NOTICE:  Total rows moved: 100000
NOTICE:  Ensure to VACUUM ANALYZE the parent (and source table if used) after partitioning data
CALL
Time: 103206.205 ms (01:43.206)


VACUUM ANALYZE public.original_table;
VACUUM
Time: 352.973 ms
```

Again, doing the commits in smaller batches like this can avoid transactions with huge row counts and long runtimes when you're partitioning a table that may have billions of rows. It's always advisable to avoid long running transactions to allow PostgreSQL's autovacuum process to work efficiently for the rest of the database. 

Using the `partition_data_proc()` PROCEDURE vs the `partition_data_id()` FUNCTION allows those commit batches. Functions in PostgreSQL always run entirely in a single transaction, even if you may tell it to do things in batches inside the function. 

Now if we check our original table, it is empty
```
SELECT count(*) FROM old_nonpartitioned_table ;
 count 
-------
     0
(1 row)
```
And the new, partitioned table with the original name has all the data and child tables created
```
SELECT count(*) FROM original_table;
 count  
--------
 100000
(1 row)


\d+ public.original_table
                                 Partitioned table "public.original_table"
 Column |           Type           | Collation | Nullable | Default | Storage  | Stats target | Description 
--------+--------------------------+-----------+----------+---------+----------+--------------+-------------
 col1   | bigint                   |           | not null |         | plain    |              | 
 col2   | text                     |           | not null |         | extended |              | 
 col3   | timestamp with time zone |           |          | now()   | plain    |              | 
 col4   | text                     |           |          |         | extended |              | 
Partition key: RANGE (col1)
Indexes:
    "original_table_col1_idx1" btree (col1)
Partitions: original_table_p0 FOR VALUES FROM ('0') TO ('10000'),
            original_table_p10000 FOR VALUES FROM ('10000') TO ('20000'),
            original_table_p100000 FOR VALUES FROM ('100000') TO ('110000'),
            original_table_p20000 FOR VALUES FROM ('20000') TO ('30000'),
            original_table_p30000 FOR VALUES FROM ('30000') TO ('40000'),
            original_table_p40000 FOR VALUES FROM ('40000') TO ('50000'),
            original_table_p50000 FOR VALUES FROM ('50000') TO ('60000'),
            original_table_p60000 FOR VALUES FROM ('60000') TO ('70000'),
            original_table_p70000 FOR VALUES FROM ('70000') TO ('80000'),
            original_table_p80000 FOR VALUES FROM ('80000') TO ('90000'),
            original_table_p90000 FOR VALUES FROM ('90000') TO ('100000'),
            original_table_default DEFAULT

SELECT count(*) FROM original_table_p10000;
 count 
-------
 10000
(1 row)


```


Now you should be able to start using your table the same as you were before!


#### Online Partitioning

Sometimes it is not possible to take the table offline for an extended period of time to migrate it to a partitioned table. Below is one method to allow this to be done online. It's not as flexible as the offline method, but should allow a very minimal downtime and be mostly transparent to the end users of the table.

As mentioned above, these methods do not account for there being foreign keys TO the original table. You can create foreign keys FROM the original table on the new partitioned table and things should work as expected. However, if you have foreign keys coming in to the table, I'm not aware of any migration method that does not require an outage to drop the original foreign keys and recreate them against the new partitioned table.

This will be a daily, time-based partition set with an IDENTITY sequence as the primary key
```
CREATE TABLE public.original_table (
    col1 bigint not null PRIMARY KEY GENERATED ALWAYS AS IDENTITY
    , col2 text not null
    , col3 timestamptz DEFAULT now() not null
    , col4 text); 

CREATE INDEX CONCURRENTLY ON public.original_table (col3);


INSERT INTO public.original_table (col2, col3, col4) VALUES ('stuff', generate_series(now() - '1 week'::interval, now(), '5 minutes'::interval), 'stuff');
```
The process is still initially the same as the offline method since you cannot turn an existing table into the parent table of a partition set. However it is critical that all constraints, privileges, defaults and any other properties be applied to the new parent table before you move on to the next step of swapping the table names around.
```
CREATE TABLE public.new_partitioned_table (
    col1 bigint not null GENERATED BY DEFAULT AS IDENTITY
    , col2 text not null
    , col3 timestamptz DEFAULT now() not null
    , col4 text) PARTITION BY RANGE (col3);

CREATE INDEX ON public.new_partitioned_table (col3);

```
You'll notice I did not set "col1" as a primary key here. That is because we cannot.
```
CREATE TABLE public.new_partitioned_table (
    col1 bigint not null PRIMARY KEY GENERATED BY DEFAULT AS IDENTITY
    , col2 text not null
    , col3 timestamptz DEFAULT now() not null
    , col4 text) PARTITION BY RANGE (col3);
ERROR:  unique constraint on partitioned table must include all partitioning columns
DETAIL:  PRIMARY KEY constraint on table "new_partitioned_table" lacks column "col3" which is part of the partition key.
```
pg_partman does have a mechanism to still apply primary/unique keys that are not part of the partition column. Just be aware that they are NOT enforced across the entire partition set; only for the individual partition. This is done with a template table. And to ensure the keys are applied when the initial child tables are created, that template table must be pre-created and its name supplied to the `create_parent()` call. We're going to use the original table as the basis and give a name similar to that so it makes sense after the name swapping later.

Another important note is that we changed the IDENTITY column from GENERATED ALWAYS to GENERATED BY DEFAULT. This is because we need to move existing values for that identity column into place. ALWAYS generally prevents manually entered values.
```
CREATE TABLE public.original_table_template (LIKE public.original_table);

ALTER TABLE public.original_table_template ADD PRIMARY KEY (col1);
```
If you do not pre-create a template table, pg_partman will always create one for you in the same schema that the extension was installed into. You can see its name by looking at the `template_table` column in the `part_config` table. However, if you add the index onto that template table after the `create_parent()` call, the already existing child tables will not have that index applied and you will have to go back and do that manually. However, any new child tables create after that will have the index. 

The tricky part here is that we cannot yet have any child tables in the partition set that match data that currently exists in the original table. This is because we're going to be adding the old table as the DEFAULT table to our new partition table. If the DEFAULT table contains any data that matches a current child table's constraints, PostgreSQL will not allow that table to be added. So, with the below `create_parent()` call, we're going to start the partition set well ahead of the data we inserted. In your case you will have to look at your current data set and pick a value well ahead of the current working set of data that may get inserted before you are able to run the table name swap process below. We're also setting the premake value to a low value to avoid having to rename too many child tables later. We'll increase premake back up to the default later (or you can set it to whatever you require).
```
select min(col3), max(col3) from original_table;
              min              |              max              
-------------------------------+-------------------------------
 2020-12-02 19:04:08.559646-05 | 2020-12-09 19:04:08.559646-05
(1 row)

SELECT partman.create_parent('public.new_partitioned_table', 'col3', 'native', 'daily', p_template_table:= 'public.original_table_template', p_premake := 1, p_start_partition := (CURRENT_TIMESTAMP+'2 days'::interval)::text);
```
The next step is to drop the DEFAULT partition that pg_partman creates for you. 
```
DROP TABLE public.new_partitioned_table_default;
```
The state of the new partitioned table should now look something like this. The current date for when this HowTo was written is given for reference:
```
SELECT CURRENT_TIMESTAMP;
       current_timestamp       
-------------------------------
 2020-12-09 19:05:15.358796-05

\d+ new_partitioned_table;
                                          Partitioned table "public.new_partitioned_table"
 Column |           Type           | Collation | Nullable |             Default              | Storage  | Stats target | Description 
--------+--------------------------+-----------+----------+----------------------------------+----------+--------------+-------------
 col1   | bigint                   |           | not null | generated by default as identity | plain    |              | 
 col2   | text                     |           | not null |                                  | extended |              | 
 col3   | timestamp with time zone |           | not null | now()                            | plain    |              | 
 col4   | text                     |           |          |                                  | extended |              | 
Partition key: RANGE (col3)
Indexes:
    "new_partitioned_table_col3_idx" btree (col3)
Partitions: new_partitioned_table_p2020_12_11 FOR VALUES FROM ('2020-12-11 00:00:00-05') TO ('2020-12-12 00:00:00-05')
```
You also need to update the `part_config` table to have the original table name. You can also update the template table if you didn't manually create one yourself, just be sure to both rename the table and update the `part_config` table as well. We'll reset the premake to the default value here as well.
```
UPDATE partman.part_config SET parent_table = 'public.original_table', premake = 4 WHERE parent_table = 'public.new_partitioned_table';
UPDATE 1
```
The next step, which is actually multiple steps in a single transaction, is the only outage of any significance that needs to be anticipated. 

1. BEGIN transaction 
1. Take an exclusive lock on the original table and new table to ensure no gaps exist for data to be misrouted
1. If using an IDENTITY column, get the original last value
1. Rename the original table to the DEFAULT table name for the partition set
1. If using an IDENTITY column, DROP the IDENTITY from the old table
1. Rename the new table to the original table's name and rename child tables & sequence to match. 
1. If using an IDENTITY column, reset the new table's identity to the latest value so any new inserts pick up where old table's sequence left off.
1. Add original table as the DEFAULT for the partition set
1. COMMIT transaction

If you are using an IDENTITY column, it is important to get its last value while the original table is locked and BEFORE you drop the old identity. Then use that returned value in the statement to RESET the IDENTITY column in the new table. A query to obtain this is provided in the SQL statements below.

If at any point there is a problem with one of these mini-steps, just perform a ROLLBACK and you should return to the previous state and allow your original table to work as it was before.

```
BEGIN;

LOCK TABLE public.original_table IN ACCESS EXCLUSIVE MODE;
LOCK TABLE public.new_partitioned_table IN ACCESS EXCLUSIVE MODE;

SELECT max(col1) FROM public.original_table;

ALTER TABLE public.original_table RENAME TO original_table_default;

-- IF using an IDENTITY column
ALTER TABLE public.original_table_default ALTER col1 DROP IDENTITY;

ALTER TABLE public.new_partitioned_table RENAME TO original_table;
ALTER TABLE public.new_partitioned_table_p2020_12_11 RENAME TO original_table_p2020_12_11;

-- IF using an IDENTITY column
ALTER SEQUENCE public.new_partitioned_table_col1_seq RENAME TO original_table_col1_seq;

-- IF using an IDENTITY column
ALTER TABLE public.original_table ALTER col1 RESTART WITH <<<VALUE OBTAINED ABOVE>>>;

ALTER TABLE public.original_table ATTACH PARTITION public.original_table_default DEFAULT;
```
```
COMMIT; or ROLLBACK;
```

Once COMMIT is run, the new partitioned table should now take over from the original non-partitioned table. And as long as all the properties have been applied to the new table, it should be working without any issues. Any new data coming in should either be going to the relevant child table, or if it doesn't happen to exist yet, it should go to the DEFAULT. The latter is not an issue since...

The next step is to partition the data out of the default. You DO NOT want to leave data in the default partition set for any length of time and especially leave any significant amount of data. If you look at the constraint that exists on a partitioned default, it is basically the anti-constraint of all other child tables. And when a new child table is added, PostgreSQL manages updating that default constraint as needed. But it must check if any data that should belong in that new child table already exists in the default. If it finds any, it will fail. But more importantly, it has to check EVERY entry in the default which can take quite a long time even with an index if there are billions of rows. During this check, there is an exclusive lock on the entire partition set.

The `partition_data_proc()` can handle moving the data out of the default. However, it cannot move data in any interval smaller than the partition interval when moving data out of the DEFAULT. This is related to what was just mentioned: You cannot add a child table to a partition set if that new child table's constraint covers data that already exists in the default. 

pg_partman handles this by first moving all the data for a given child table out to a temporary table, then creating the child table, and then moving the data from the temp table into the new child table. Since we're moving data out of the DEFAULT and we cannot use a smaller interval, the only parameter that we need to pass is a batch size. The default batch size of 1 would only make a single child table then stop. If you want to move all the data in a single call, just pass a value large enough to cover the expected number of child tables. However, with a live table and LOTS rows, this could potentially generate A LOT of WAL files, especially since this method doubles the number of writes vs the offline method (default -> temp -> child table). So if keeping control of your disk usage is a concern, just give a smaller batch value and then give PostgreSQL some time to run through a few CHECKPOINTs and clean up its own WAL before moving on to the next batch. 
```
CALL partman.partition_data_proc('public.original_table', p_batch := 200);

NOTICE:  Batch: 1, Rows moved: 60
NOTICE:  Batch: 2, Rows moved: 288
NOTICE:  Batch: 3, Rows moved: 288
NOTICE:  Batch: 4, Rows moved: 288
NOTICE:  Batch: 5, Rows moved: 288
NOTICE:  Batch: 6, Rows moved: 288
NOTICE:  Batch: 7, Rows moved: 288
NOTICE:  Batch: 8, Rows moved: 229
NOTICE:  Total rows moved: 2017
NOTICE:  Ensure to VACUUM ANALYZE the parent (and source table if used) after partitioning data
CALL
Time: 8432.725 ms (00:08.433)

VACUUM ANALYZE original_table;
VACUUM
Time: 60.690 ms
```
If you were using an IDENTITY column with GENERATED ALWAYS before, you'll want to change the identity on the partitioned table back to that from the current setting of BY DEFAULT
```
ALTER TABLE public.original_table ALTER col1 SET GENERATED ALWAYS;
```
Now, double-check that there are no child table names that don't conform to the pattern that pg_partman uses for the new child tables. pg_partman expects a specific format for the child tables it manages, so if they don't all match, maintenance may not work as expected
```
\d+ original_table;
                                              Partitioned table "public.original_table"
 Column |           Type           | Collation | Nullable |             Default              | Storage  | Stats target | Description 
--------+--------------------------+-----------+----------+----------------------------------+----------+--------------+-------------
 col1   | bigint                   |           | not null | generated by default as identity | plain    |              | 
 col2   | text                     |           | not null |                                  | extended |              | 
 col3   | timestamp with time zone |           | not null | now()                            | plain    |              | 
 col4   | text                     |           |          |                                  | extended |              | 
Partition key: RANGE (col3)
Indexes:
    "new_partitioned_table_col3_idx" btree (col3)
Partitions: original_table_p2020_12_02 FOR VALUES FROM ('2020-12-02 00:00:00-05') TO ('2020-12-03 00:00:00-05'),
            original_table_p2020_12_03 FOR VALUES FROM ('2020-12-03 00:00:00-05') TO ('2020-12-04 00:00:00-05'),
            original_table_p2020_12_04 FOR VALUES FROM ('2020-12-04 00:00:00-05') TO ('2020-12-05 00:00:00-05'),
            original_table_p2020_12_05 FOR VALUES FROM ('2020-12-05 00:00:00-05') TO ('2020-12-06 00:00:00-05'),
            original_table_p2020_12_06 FOR VALUES FROM ('2020-12-06 00:00:00-05') TO ('2020-12-07 00:00:00-05'),
            original_table_p2020_12_07 FOR VALUES FROM ('2020-12-07 00:00:00-05') TO ('2020-12-08 00:00:00-05'),
            original_table_p2020_12_08 FOR VALUES FROM ('2020-12-08 00:00:00-05') TO ('2020-12-09 00:00:00-05'),
            original_table_p2020_12_09 FOR VALUES FROM ('2020-12-09 00:00:00-05') TO ('2020-12-10 00:00:00-05'),
            original_table_p2020_12_11 FOR VALUES FROM ('2020-12-11 00:00:00-05') TO ('2020-12-12 00:00:00-05'),
```
And now to ensure any new data coming in is going to proper child tables and not the default, run maintenance on the new partitioned table to ensure the current premake partitions are created
```
SELECT partman.run_maintenance('public.original_table');

\d+ original_table;
                                              Partitioned table "public.original_table"
 Column |           Type           | Collation | Nullable |             Default              | Storage  | Stats target | Description 
--------+--------------------------+-----------+----------+----------------------------------+----------+--------------+-------------
 col1   | bigint                   |           | not null | generated by default as identity | plain    |              | 
 col2   | text                     |           | not null |                                  | extended |              | 
 col3   | timestamp with time zone |           | not null | now()                            | plain    |              | 
 col4   | text                     |           |          |                                  | extended |              | 
Partition key: RANGE (col3)
Indexes:
    "new_partitioned_table_col3_idx" btree (col3)
Partitions: original_table_p2020_12_02 FOR VALUES FROM ('2020-12-02 00:00:00-05') TO ('2020-12-03 00:00:00-05'),
            original_table_p2020_12_03 FOR VALUES FROM ('2020-12-03 00:00:00-05') TO ('2020-12-04 00:00:00-05'),
            original_table_p2020_12_04 FOR VALUES FROM ('2020-12-04 00:00:00-05') TO ('2020-12-05 00:00:00-05'),
            original_table_p2020_12_05 FOR VALUES FROM ('2020-12-05 00:00:00-05') TO ('2020-12-06 00:00:00-05'),
            original_table_p2020_12_06 FOR VALUES FROM ('2020-12-06 00:00:00-05') TO ('2020-12-07 00:00:00-05'),
            original_table_p2020_12_07 FOR VALUES FROM ('2020-12-07 00:00:00-05') TO ('2020-12-08 00:00:00-05'),
            original_table_p2020_12_08 FOR VALUES FROM ('2020-12-08 00:00:00-05') TO ('2020-12-09 00:00:00-05'),
            original_table_p2020_12_09 FOR VALUES FROM ('2020-12-09 00:00:00-05') TO ('2020-12-10 00:00:00-05'),
            original_table_p2020_12_11 FOR VALUES FROM ('2020-12-11 00:00:00-05') TO ('2020-12-12 00:00:00-05'),
            original_table_p2020_12_12 FOR VALUES FROM ('2020-12-12 00:00:00-05') TO ('2020-12-13 00:00:00-05'),
            original_table_p2020_12_13 FOR VALUES FROM ('2020-12-13 00:00:00-05') TO ('2020-12-14 00:00:00-05'),
            original_table_default DEFAULT
```

Before this, depending on the child tables that were generated and the new data coming in, there may have been some data that still went to the default. You can check for that with a function that comes with pg_partman:
```
SELECT * FROM partman.check_default(p_exact_count := true);
```
If you don't pass "true" to the function, it just returns a 1 or 0 to indicate if any data exists in any default. This is convenient for monitoring situations and it can also be quicker since it stops checking as soon as it finds data in any child table. However, in this case we want to see exactly what our situation is, so passing true will give us an exact count of how many rows are left in the default.

You'll also notice that there is a child table missing in the set above (Dec 10, 2020). This is because we set the partition table to start 2 days ahead and we didn't have any data for that date in the original table. You can fix this one of two ways:

1. Wait for data for that time period to be inserted and once you're sure that interval is done, partition the data out of the DEFAULT the same way we did before.
1. Run the `partition_gap_fill()` function to fill any gaps immediately:
```
SELECT * FROM partman.partition_gap_fill('public.original_table');
 partition_gap_fill 
--------------------
                  1
(1 row)

\d+ original_table;
                                              Partitioned table "public.original_table"
 Column |           Type           | Collation | Nullable |             Default              | Storage  | Stats target | Description 
--------+--------------------------+-----------+----------+----------------------------------+----------+--------------+-------------
 col1   | bigint                   |           | not null | generated by default as identity | plain    |              | 
 col2   | text                     |           | not null |                                  | extended |              | 
 col3   | timestamp with time zone |           | not null | now()                            | plain    |              | 
 col4   | text                     |           |          |                                  | extended |              | 
Partition key: RANGE (col3)
Indexes:
    "new_partitioned_table_col3_idx" btree (col3)
Partitions: original_table_p2020_12_02 FOR VALUES FROM ('2020-12-02 00:00:00-05') TO ('2020-12-03 00:00:00-05'),
            original_table_p2020_12_03 FOR VALUES FROM ('2020-12-03 00:00:00-05') TO ('2020-12-04 00:00:00-05'),
            original_table_p2020_12_04 FOR VALUES FROM ('2020-12-04 00:00:00-05') TO ('2020-12-05 00:00:00-05'),
            original_table_p2020_12_05 FOR VALUES FROM ('2020-12-05 00:00:00-05') TO ('2020-12-06 00:00:00-05'),
            original_table_p2020_12_06 FOR VALUES FROM ('2020-12-06 00:00:00-05') TO ('2020-12-07 00:00:00-05'),
            original_table_p2020_12_07 FOR VALUES FROM ('2020-12-07 00:00:00-05') TO ('2020-12-08 00:00:00-05'),
            original_table_p2020_12_08 FOR VALUES FROM ('2020-12-08 00:00:00-05') TO ('2020-12-09 00:00:00-05'),
            original_table_p2020_12_09 FOR VALUES FROM ('2020-12-09 00:00:00-05') TO ('2020-12-10 00:00:00-05'),
            original_table_p2020_12_10 FOR VALUES FROM ('2020-12-10 00:00:00-05') TO ('2020-12-11 00:00:00-05'),
            original_table_p2020_12_11 FOR VALUES FROM ('2020-12-11 00:00:00-05') TO ('2020-12-12 00:00:00-05'),
            original_table_p2020_12_12 FOR VALUES FROM ('2020-12-12 00:00:00-05') TO ('2020-12-13 00:00:00-05'),
            original_table_p2020_12_13 FOR VALUES FROM ('2020-12-13 00:00:00-05') TO ('2020-12-14 00:00:00-05'),
            original_table_default DEFAULT
```
You can see that created the missing table for Dec 10th.

At this point your new partitioned table should already have been in use and working without any issues!
```
INSERT INTO original_table (col2, col3, col4) VALUES ('newstuff', now(), 'newstuff');
INSERT INTO original_table (col2, col3, col4) VALUES ('newstuff', now(), 'newstuff');
SELECT * FROM original_table ORDER BY col1 DESC limit 5;
```


### Undoing Native Partitioning

Just as a normal table cannot be converted to a natively partitioned table, the opposite is also true. To undo native partitioning, you must move the data to a brand new table. There may be a way to do this online, but I do not currently have such a method planned out. If someone would like to submit a method or request that I look into it further, please feel free to make an issue on Github. The below method shows how to undo the daily partitioned example above, including handling an IDENTITY column if necessary.

First, we create a new table to migrate the data to. We can set a primary key, or any unique indexes that were made on the template. If there are any identity columns, they have to set the method to `GENERATED BY DEFAULT` since we will be adding values in manually as part of the migration. If it needs to be `ALWAYS`, this can be changed later.

If this table is going to continue to be used as the partitioned table, ensure all privileges, constraints & indexes are created on this table as well. Index & constraint creation can be delayed until after the data has been moved to speed up the migration.
```
CREATE TABLE public.new_regular_table (
    col1 bigint not null GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY
    , col2 text not null
    , col3 timestamptz DEFAULT now() not null
    , col4 text);

CREATE INDEX ON public.new_regular_table (col3);
```

Now we can use the `undo_partition_proc()` procedure to move the data out of our partitioned table to the regular table.  We can even  chose a smaller interval size for this as well to reduce the transaction runtime for each batch. The batch size is a default of 1, which would only run the given interval one time. We want to undo the entire thing with one call, so pass a number at high enough to run through all batches. It will stop when all the data has been moved, even if you passed a higher batch number. We also don't need to keep the old child tables once they're empty, so that is set to false. See the documentation for more information on other options for the undo functions/procedure.
```
CALL partman.undo_partition_proc('public.original_table', p_interval := '1 hour'::text, p_batch := 500, p_target_table := 'public.new_regular_table', p_keep_table := false);

NOTICE:  Moved 13 row(s) to the target table. Removed 1 partitions.
NOTICE:  Batch: 1, Partitions undone this batch: 1, Rows undone this batch: 13
NOTICE:  Moved 13 row(s) to the target table. Removed 0 partitions.
NOTICE:  Batch: 2, Partitions undone this batch: 0, Rows undone this batch: 13
NOTICE:  Moved 13 row(s) to the target table. Removed 0 partitions.
NOTICE:  Batch: 3, Partitions undone this batch: 0, Rows undone this batch: 13
[...]
NOTICE:  Batch: 160, Partitions undone this batch: 0, Rows undone this batch: 13
NOTICE:  Moved 5 row(s) to the target table. Removed 1 partitions.
NOTICE:  Batch: 161, Partitions undone this batch: 1, Rows undone this batch: 5
NOTICE:  Moved 0 row(s) to the target table. Removed 4 partitions.
NOTICE:  Total partitions undone: 13, Total rows moved: 2017
NOTICE:  Ensure to VACUUM ANALYZE the old parent & target table after undo has finished
CALL
Time: 163465.195 ms (02:43.465)

VACUUM ANALYZE original_table;
VACUUM
Time: 20.706 ms
VACUUM ANALYZE new_regular_table;
VACUUM
Time: 20.375 ms
```
Now object names can be swapped around and the identity sequence reset and method changed if needed. Be sure to grab the original sequence value and use that when resetting.
```
SELECT max(col1) FROM public.original_table;

ALTER TABLE original_table RENAME TO old_partitioned_table;
ALTER SEQUENCE original_table_col1_seq RENAME TO old_partitioned_table_col1_seq;

ALTER TABLE new_regular_table RENAME TO original_table;
ALTER SEQUENCE new_regular_table_col1_seq RENAME TO original_table_col1_seq;
ALTER TABLE public.original_table ALTER col1 RESTART WITH <<<VALUE OBTAINED ABOVE>>>;
ALTER TABLE public.original_table ALTER col1 SET GENERATED ALWAYS;
```
```
INSERT INTO original_table (col2, col3, col4) VALUES ('newstuff', now(), 'newstuff');
INSERT INTO original_table (col2, col3, col4) VALUES ('newstuff', now(), 'newstuff');
SELECT * FROM original_table ORDER BY col1 DESC limit 2;
```

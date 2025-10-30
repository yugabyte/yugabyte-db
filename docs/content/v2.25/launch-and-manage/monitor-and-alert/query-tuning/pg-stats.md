---
title: Get column statistics using pg_stats
linkTitle: Column statistics
headerTitle: Column statistics using pg_stats
headcontent: Improve queries using column level statistics of tables from pg_stats view
menu:
  v2.25:
    identifier: pg-stats
    parent: query-tuning
    weight: 400
type: docs
---

[pg_stats](../../../../architecture/system-catalog#data-statistics) is a system view that provides statistical information about columns in tables. Use it to view data distributions of column values, which can help in query optimization and tuning.

To generate the statistics, run the [ANALYZE](../../../../api/ysql/the-sql-language/statements/cmd_analyze/) command.

To automatically update the statistics, configure the [Auto Analyze service](../../../../additional-features/auto-analyze/).

## Key statistics

pg_stats contains information such as the following:

- **null_frac**: Fraction of NULL values in a column.
- **avg_width**: Average width (in bytes) of a column's entries.
- **n_distinct**: Estimated number of distinct values in a column. Positive values are direct counts, while negative values indicate that the distinct count is a fraction of the total row count.
- **most_common_vals**: Most frequently occurring values in a column.
- **most_common_freqs**: Frequencies of the most common values.
- **most_common_elems**: Most frequently occurring elements in an array column.
- **most_common_elem_freqs**: Frequencies of the most elements in an array column.

Run the following examples to understand how you can use these statistics to improve queries and the data model.

## Setup

{{% explore-setup-single-new %}}

Create a users table:

```sql
CREATE TABLE users (
    id int,
    name VARCHAR,
    employed BOOLEAN,
    PRIMARY KEY(id)
);
```

Add some data to your table so that you can correlate it during analysis.

```sql
-- set seed for random to be repeatable
SELECT setseed(0.5); -- to help generate the same random values

-- Insert 10,000 rows into the users table
INSERT INTO users (id, name, employed)
SELECT
    id,
    CASE
        -- 40% NULL names
        WHEN random() < 0.4 THEN NULL
        -- 60% non-null names with skewed distribution
        ELSE (
            CASE
                -- 30% concentration of empty string
                WHEN random() < 0.5 THEN ''
                -- 9 names for the remaining 30%
                ELSE (ARRAY['Sam', 'Kim', 'Pat', 'Lee', 'Morgan',
                            'Taylor', 'Jordan', 'Casey', 'Jamie']
                )[floor(random() * 9 + 1)::int]
            END
        )
    END AS name,
    (ARRAY[true, false])[id % 2 + 1] AS employed
FROM generate_series(1, 10000) AS id;
```

Run ANALYZE to gather statistics on the column values of the table as follows:

```sql
ANALYZE users;
```

Turn ON extended display for better readability as follows:

```sh
@yugabyte=> \x ON
```

## Fetching the statistics

When the ANALYZE command is run, the pg_statistic catalog is populated with the statistics. These statistics are made accessible via the pg_stats view. For example, you can view the statistics of the `name` column in the `users` table as follows:

```sql
SELECT attname, null_frac, n_distinct, most_common_vals, most_common_freqs
    FROM pg_stats
    WHERE tablename='users' AND attname='name';
```

You should get an output similar to the following:

```caddyfile{.nocopy}
-[ RECORD 1 ]-----+------------------------------------------------------------------------
attname           | name
null_frac         | 0.4042
n_distinct        | 10
most_common_vals  | {"",Jordan,Jamie,Morgan,Taylor,Kim,Pat,Casey,Sam,Lee}
most_common_freqs | {0.2963,0.0352,0.0346,0.0342,0.0339,0.0334,0.0333,0.0328,0.0313,0.0308}
```

The preceding output shows the following:

- **null_frac** shows that the fraction of null values is about 40%. (Recall that you tried to insert about 40% NULL values.)
- **n_distinct** shows that there are about 10 distinct values in the name column.
- **most_common_vals** shows the most commonly occurring values (other than NULL).
- **most_common_freqs** shows the frequency with which the most common values occur. For example, Empty value occurs at 29.6%, Jordan occurs at 3.5%, and so on.

## Partial indexes

If you were to try to create an index on the column `name`, the index could be unevenly distributed. This is because 40% of the dataset consists of NULL values, and 30% consists of empty values, with only 30% being valid values. If you know that your queries will not look up NULL or empty values, you can create a partial index as follows:

```sql
CREATE INDEX idx_users_name_nonempty
    ON users (name)
    WHERE name IS NOT NULL AND name <> '';
```

This index will include only the valid values and as a result won't be skewed.

## Cardinality

If you fetch the statistics on the `id` and `employed` columns as follows:

```sql
SELECT attname, null_frac, n_distinct, most_common_vals, most_common_freqs
    FROM pg_stats
    WHERE tablename='users' AND attname IN ('id', 'employed');
```

You will get an output similar to the following:

```caddyfile{.nocopy}
-[ RECORD 1 ]-----+----------
attname           | id
null_frac         | 0
n_distinct        | -1
most_common_vals  | null
most_common_freqs | null

-[ RECORD 2 ]-----+----------
attname           | employed
null_frac         | 0
n_distinct        | 2
most_common_vals  | {f,t}
most_common_freqs | {0.5,0.5}
```

The `n_distinct = -1` value for the `id` column indicates that all IDs in the table are unique. A negative `n_distinct` value represents the ratio of distinct items to the total number of rows in the table.

For the employed column, there are 2 distinct values. If an index is created on this column, it will be distributed across only 2 nodes, as the values will generate only 2 distinct hashes. This is the reason why you shouldn't create indexes on low cardinality columns like Booleans (2) or Days of week (7).

## Skewed data

Ideally, your index/table should be reasonably distributed so that the nodes in the cluster process a similar amount of queries. Using pg_stats, you can quickly determine that empty names are about 30% of the dataset. If you create an index on this `name` that includes empty values, all the empty values will be one single node. Any queries for empty names will go to that one node. Depending on your use case, this may or may not be ideal. In such scenarios you can consider a composite index involving more than one column, so that the index for the same `name` gets distributed across multiple nodes like:

```sql
CREATE INDEX idx_users_name_employed
    ON users ((name, employed));
```

This will ensure that the index on the same value of `name` will be distributed across at least 2 nodes (as employed has only 2 distinct values). Although we have added a [low cardinality](#cardinality) value, `employed` onto the index, it is advisable to have higher cardinality values in the index.

## Composition of arrays

Similar to how pg_stats reports common values, it also reports commonly occurring elements and their respective frequencies within array data types in the `most_common_elems` and `most_common_elem_freqs` columns. For example, consider a table that stores the labels that movies are tagged with:

```sql
CREATE TABLE labels (
    id SERIAL PRIMARY KEY,
    tags TEXT[]
);

INSERT INTO labels (tags) VALUES
    (ARRAY['romance', 'comedy', 'action']),
    (ARRAY['romance', 'action']),
    (ARRAY['romance', 'thriller']),
    (ARRAY['comedy', 'thriller']),
    (ARRAY['action', 'thriller']);
```

Now, run ANALYZE on the above table as follows:

```sql
ANALYZE labels;
```

You can fetch the statistics for the elements as follows:

```sql
SELECT attname, most_common_elems, most_common_elem_freqs FROM pg_stats WHERE tablename = 'labels' AND attname = 'tags';
```

You should see an output similar to the following:

```caddyfile{.nocopy}
-[ RECORD 1 ]----------+---------------------------------
attname                | tags
most_common_elems      | {action,comedy,romance,thriller}
most_common_elem_freqs | {0.6,0.4,0.6,0.6,0.4,0.6,0}
```

This indicates that `action` appears in 60% of the records (3 in this case) and `comedy` in 40% of the records (2 in this case), providing insight into the data distribution. Notice that there are 4 entries in the `most_common_elems` field but 7 entries in `most_common_elem_freqs`. The first 4 values correspond to the frequencies of the 4 most common elements, followed by the minimum and maximum element frequencies, with the last value representing the frequency of NULLs.

## Learn more

- [Cost Based Optimizer](https://www.yugabyte.com/blog/yugabytedb-cost-based-optimizer/)

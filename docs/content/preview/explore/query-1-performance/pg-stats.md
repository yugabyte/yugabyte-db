---
title: Get column statistics using pg_stats
linkTitle: Column stats
headerTitle: Column statistics using pg_stats
headcontent: Improve queries using column level statistics of tables from pg_stats view
menu:
  preview:
    identifier: pg-stats
    parent: query-tuning
    weight: 400
type: docs
---

[pg_stats](../../../architecture/system-catalog#data-statistics) is a system view that provides statistical information about columns in tables. It’s useful for understanding data distributions of column values, which can help in query optimization and tuning. These statistics are generated on running the [ANALYZE](../../../api/ysql/the-sql-language/statements/cmd_analyze/) command.

## Key statistics

pg_stats contains information like:

- **null_frac**: Fraction of NULL values in a column.
- **avg_width**: Average width (in bytes) of a column’s entries.
- **n_distinct**: Estimated number of distinct values in a column. Positive values are direct counts, while negative values indicate that the distinct count is a fraction of the total row count.
- **most_common_vals**: Most frequently occurring values in a column.
- **most_common_freqs**: Frequencies of the most common values.

To understand how to use these statistics to improve queries and data model, let's go over some examples.

## Setup

The examples run on any YugabyteDB universe.

<!-- begin: nav tabs -->
{{<nav/tabs list="local,anywhere,cloud" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
{{<setup/local numnodes="1" rf="1" >}}

{{</nav/panel>}}

{{<nav/panel name="anywhere">}} {{<setup/anywhere>}} {{</nav/panel>}}
{{<nav/panel name="cloud">}}{{<setup/cloud>}}{{</nav/panel>}}
{{</nav/panels>}}
<!-- end: nav tabs -->

For illustration, let us create a users table as:

```sql
CREATE TABLE users (
    id int,
    name VARCHAR,
    PRIMARY KEY(id)
);
```

Let us add some specific data to this table so that we can corelate to it during analysis.

```sql
-- set seed for random to be repeatable
SELECT setseed(0.5); -- to help generate the same random values

-- Insert 10,000 rows into the users table
INSERT INTO users (id, name)
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
    END AS name
FROM generate_series(1, 10000) AS id;
```

Run analyze to gather statistics on the column values of the table as:

```sql
ANALYZE users;
```

Also turn ON extended display for better readability as:

```sh
@yugabyte=> \x ON
```

## Fetching the statistics

Once the ANALYZE command is run, the `pg_statistic` command is populated with the statistics of the data. These stats are made easily readbable via the pg_stats view. For example, you can view the statistics of the `name` column in the `users` table as,

```sql
SELECT attname, null_frac, n_distinct, most_common_vals, most_common_freqs
    FROM pg_stats
    WHERE tablename='users' AND attname='name';
```

You should get an output similar to:

```caddyfile{.nocopy}
-[ RECORD 1 ]-----+------------------------------------------------------------------------
attname           | name
null_frac         | 0.4042
n_distinct        | 10
most_common_vals  | {"",Jordan,Jamie,Morgan,Taylor,Kim,Pat,Casey,Sam,Lee}
most_common_freqs | {0.2963,0.0352,0.0346,0.0342,0.0339,0.0334,0.0333,0.0328,0.0313,0.0308}
```

The above output says that,

- **null_frac** shows that the fraction of null values is about 40%. (_Remember_, we tried to insert about 40% NULL values)
- **n_distinct** shows that there are about 10 distinct values in the name column
- **most_common_vals** shows the most commonly occuring values (other than NULL)
- **most_common_freqs** shows the frequency in which the most common values occur. For example, Empty value occurs at 29.6%, John occurs at 3.0% and so on.

## Partial indexes

Now when we try to create an index on the column `name`, the index could be unevenly distributed. This is due to 40% of the dataset consisting of NULL values and 30% consisting of empty values. Only 30% consist valid values. If we know that our queries will not lookup NULL or empty values, we can create partial index as,

```sql
CREATE INDEX idx_users_name_nonempty
    ON users (name)
    WHERE name IS NOT NULL AND name <> '';
```

The above index will have just the valid values and the index will not be skewed.

## Cardinality

Given that there are only 9 (via **n_distinct**) distinct valid (without empty string) values in the `name` column, on an index with HASH distribution, even if there are more nodes in the cluster, the index will end up being only on a maximum of 9 nodes. This is because each value is hashed to determine the tablet in which it should be stored and there will be only 9 distinct hashes. This is the reason why is not advisable to create indexes on low cardinality columns like Booleans (2), Days of week(7) etc.

## Skewed data

It is always advisable for your index/table to be reasonably distributed so that all nodes in your cluster handle a reasonably equal share of queries. In the above data set, it is easy to see that together `NULL` and empty values account for 70% of the total data set (via **most_common_vals, null_frac**). And the valid names constitute only 30% of the dataset. If an index was created will all these values then all queries to the index will be handled only by about 30% of the nodes in the cluster as 70% of data would not be queried against.

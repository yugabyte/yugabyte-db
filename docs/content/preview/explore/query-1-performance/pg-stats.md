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

Let us add some specific data to this data so that we can corelate to it during analysis.

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
                -- 60% concentration of 2 names
                WHEN random() < 0.6 THEN (ARRAY['John', ''])[floor(random() * 4 + 1)::int]
                -- Remaining 16 names for 40% of names
                ELSE (ARRAY['Sam', 'Kim', 'Pat', 'Lee', 'Morgan', 'Taylor', 'Jordan', 'Casey', 'Jamie'])[floor(random() * 9 + 1)::int]
            END
        )
    END AS name
FROM generate_series(1, 10000) AS id;
```

The above statement inserts 40% NULL values and

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
-[ RECORD 1 ]-----+-------------------------------------------------------------------------------
attname           | name
null_frac         | 0.5784
n_distinct        | 11
most_common_vals  | {"",John,Casey,Jamie,Jordan,Pat,Taylor,Kim,Sam,Morgan,Lee}
most_common_freqs | {0.0948,0.0903,0.0283,0.0278,0.0273,0.0266,0.0264,0.0256,0.0254,0.0253,0.0238}
```

The above output says that,

- **null_frac** shows that the fraction of null values is about 57.8%. (_Remember_, we tried to insert about 60% NULL values)
- **n_distinct** shows that there are about 11 distinct values in the name column
- **most_common_vals** shows the most commonly occuring values (other than NULL)
- **most_common_freqs** shows the frequency in which the most common values occur. For example, Empty value occurs at 9.4%, John occurs at 9.0% and so on.

## Partial indexes

Now when we try to create an index on the column `name`, the index could be unevenly distributed. This is due to 60% of the dataset consisting of NULL values and 10% consisting of empty values. Only 40% consist valid values. If we know that our queries will not lookup NULL or empty values, we can create partial index as,

```sql
CREATE INDEX idx_users_name_nonempty
    ON users (name)
    WHERE name IS NOT NULL AND name <> '';
```

The above index will have just the valid values and the index will not be skewed.

## Cardinality

Given that there are only 11 distinct values (via **n_distinct**) in the `name` column, on an index with HASH distribution, even if there are more nodes in the cluster, the index will end up being only on a maximum of 11 nodes. This is because each value is hashed to determine the tablet in which it should be stored and there will be only 11 distinct hashes. This is the reason why is not advisable to create indexes on low cardinality columns like Booleans (2), Days of week(7) etc.

## Skewed data

It is always advisable for your index/table to be reasonably distributed so that all nodes in your cluster handle a reasonably equal share of queries. In the above data set, it is easy to see that each `John` and `Casey` account for 10% of the total data set (via **most_common_vals, most_common_freqs**) and about 25% of the valid values. This means that on an even distribution of queries across all names, about 50% of the queries will be handled maximum of just tablets (the ones that contain `John` and `Casey`). It would be worse if they happen to be on the same tablet.

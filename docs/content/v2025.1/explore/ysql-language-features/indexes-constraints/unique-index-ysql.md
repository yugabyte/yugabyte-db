---
title: Unique indexes in YugabyteDB YSQL
headerTitle: Unique indexes
linkTitle: Unique indexes
description: Using Unique indexes in YSQL
headContent: Explore unique indexes in YugabyteDB using YSQL
menu:
  v2025.1:
    identifier: unique-index-ysql
    parent: explore-indexes-constraints-ysql
    weight: 230
type: docs
---

If you need values in some of the columns to be unique, you can create a UNIQUE index on that column. The behavior is more of a constraint than an index. If a table has a primary key or a [UNIQUE constraint](../../data-manipulation/#constraints) defined, a corresponding unique index is created automatically.

When a unique index is applied to two or more columns, the combined values in these columns can't be duplicated in multiple rows. Note that because a NULL value is treated as a distinct value, you can have multiple NULL values in a column with a unique index.

## Syntax

```sql
CREATE UNIQUE INDEX index_name ON table_name(column_list)  [ NULLS [ NOT ] DISTINCT ];
```

When creating an unique index, by default, null values are not considered equal, allowing multiple nulls in the column. The NULLS NOT DISTINCT option modifies this and causes the index to treat nulls as equal.

## Setup

{{% explore-setup-single-new %}}

This example uses the `categories` table from the [Northwind sample database](../../../../sample-data/northwind/#install-the-northwind-sample-database).

View the contents of the `categories` table:

```sql
northwind=# SELECT * FROM categories  LIMIT 5;
```

```caddyfile{.nocopy}
 category_id | category_name  |                        description                         | picture
-------------+----------------+------------------------------------------------------------+---------
           4 | Dairy Products | Cheeses                                                    | \x
           1 | Beverages      | Soft drinks, coffees, teas, beers, and ales                | \x
           2 | Condiments     | Sweet and savory sauces, relishes, spreads, and seasonings | \x
           7 | Produce        | Dried fruit and bean curd                                  | \x
           3 | Confections    | Desserts, candies, and sweet breads                        | \x
(5 rows)
```

Create a UNIQUE index for the `category_id` column in the `categories` table.

```sql
northwind=# CREATE UNIQUE INDEX index_category_id
              ON categories(category_id);
```

Now, any attempt to insert a new category with an existing `category_id` results in an error.

```sql
northwind=# INSERT INTO categories(category_id, category_name, description) VALUES (1, 'Savories', 'Spicy chips and snacks');
```

```sql{.nocopy}
ERROR:  duplicate key value violates unique constraint "categories_pkey"
```

Insert a row with a new `category_id` and verify its existence in the table.

```sql
northwind=# INSERT INTO categories(category_id, category_name, description) VALUES (9, 'Savories', 'Spicy chips and snacks');
```

```sql
northwind=# SELECT * FROM categories;
```

```caddyfile{.nocopy}
 category_id | category_name  |                        description                         | picture
-------------+----------------+------------------------------------------------------------+---------
           4 | Dairy Products | Cheeses                                                    | \x
           1 | Beverages      | Soft drinks, coffees, teas, beers, and ales                | \x
           2 | Condiments     | Sweet and savory sauces, relishes, spreads, and seasonings | \x
           7 | Produce        | Dried fruit and bean curd                                  | \x
           9 | Savories       | Spicy chips and snacks                                     |
           3 | Confections    | Desserts, candies, and sweet breads                        | \x
           8 | Seafood        | Seaweed and fish                                           | \x
           5 | Grains/Cereals | Breads, crackers, pasta, and cereal                        | \x
           6 | Meat/Poultry   | Prepared meats                                             | \x
```

## Learn more

- [Unique index with HASH column ordering](../../../../api/ysql/the-sql-language/statements/ddl_create_index/#unique-index-with-hash-column-ordering)
- [UNIQUE constraint](../../data-manipulation#constraints)
- [Indexes on JSON attributes](../../jsonb-ysql/#indexes-on-json-attributes)
- [Benefits of Index-only scan](https://www.yugabyte.com/blog/how-a-distributed-sql-database-boosts-secondary-index-queries-with-index-only-scan/)
- [CREATE TABLE](../../../../api/ysql/the-sql-language/statements/ddl_create_table/)

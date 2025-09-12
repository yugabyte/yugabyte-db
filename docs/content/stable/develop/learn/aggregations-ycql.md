---
title: Aggregations in YCQL
headerTitle: Aggregations
linkTitle: Aggregations
description: Learn how YugabyteDB YCQL supports standard aggregation functions.
menu:
  preview:
    identifier: aggregations-1-ycql
    parent: learn
    weight: 567
type: docs
---

{{<api-tabs list="ycql">}}

YugabyteDB supports a number of standard aggregation functions. For example, consider the following products table:

| ProductID | ProductName          | SupplierID | CategoryID | Unit | Price | Quantity
| ---       | ---                  | ---        | ---        | ---  | --- | ---
| 1 | Chais                        | 1 | 1 | 10 boxes x 20 bags  | 18    | 25
| 2 | Chang                        | 1 | 1 | 24 - 12 oz bottles  | 19    | 12
| 3 | Aniseed Syrup                | 1 | 2 | 12 - 550 ml bottles | 10    | 10
| 4 | Chef Anton's Cajun Seasoning | 2 | 2 | 48 - 6 oz jars      | 22    | 9
| 5 | Chef Anton's Gumbo Mix       | 2 | 2 | 36 boxes            | 21.35 | 40

Let us create this table with `ProductID` as the primary hash key.

```sql
ycqlsh> CREATE KEYSPACE store;
```

```sql
ycqlsh> CREATE TABLE store.products (ProductID BIGINT PRIMARY KEY, ProductName VARCHAR, SupplierID INT, CategoryID INT, Unit TEXT, Price FLOAT, Quantity INT);
```

Now populate the sample data.

```sql
INSERT INTO store.products (ProductID, ProductName, SupplierID, CategoryID, Unit, Price, Quantity) VALUES (1, 'Chais', 1, 1, '10 boxes x 20 bags', 18, 25);
INSERT INTO store.products (ProductID, ProductName, SupplierID, CategoryID, Unit, Price, Quantity) VALUES (2, 'Chang', 1, 1, '24 - 12 oz bottles', 19, 12);
INSERT INTO store.products (ProductID, ProductName, SupplierID, CategoryID, Unit, Price, Quantity) VALUES (3, 'Aniseed Syrup', 1, 2, '12 - 550 ml bottles', 10, 10);
INSERT INTO store.products (ProductID, ProductName, SupplierID, CategoryID, Unit, Price, Quantity) VALUES (4, 'Chef Anton''s Cajun Seasoning', 2, 2, '48 - 6 oz jars', 22, 9);
INSERT INTO store.products (ProductID, ProductName, SupplierID, CategoryID, Unit, Price, Quantity) VALUES (5, 'Chef Anton''s Gumbo Mix', 2, 2, '36 boxes', 21.35, 40);
```

## Counts

- Find the number of item types in the store as follows:

    ```sql
    ycqlsh> SELECT COUNT(ProductID) FROM store.products;
    ```

    ```output
    count(productid)
    ------------------
                    5

    (1 rows)
    ```

- Give an alias name to the count column as follows:

    ```sql
    ycqlsh> SELECT COUNT(ProductID) as num_products FROM store.products;
    ```

    ```output
    num_products
    --------------
                5

    (1 rows)
    ```

- Find the number of item types for supplier 1 as follows:

    ```sql
    ycqlsh> SELECT COUNT(ProductID) as supplier1_num_products FROM store.products WHERE SupplierID=1;
    ```

    ```output
    supplier1_num_products
    ------------------------
                          3

    (1 rows)
    ```

## Numeric aggregation functions

The standard aggregate functions of `min`, `max`, `sum`, `avg` and `count` are built-in functions.

- To find the total number of items in the store, run the following query:

    ```sql
    ycqlsh> SELECT SUM(Quantity) FROM store.products;
    ```

    ```output
    sum(quantity)
    ---------------
                96

    (1 rows)
    ```

- To find the price of the cheapest and the most expensive item, run the following:

    ```sql
    ycqlsh> SELECT MIN(Price), MAX(Price) FROM store.products;
    ```

    ```output
    min(price) | max(price)
    ------------+------------
            10 |         22

    (1 rows)
    ```

- To find the average price of all the items in the store, run the following:

    ```sql
    ycqlsh> SELECT AVG(price) FROM store.products;
    ```

    ```output
    system.avg(price)
    -------------------
                18.07

    (1 rows)
    ```

## 1. Setup - create universe

If you have a previously running local universe, destroy it using the following.

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl destroy
```

Start a new local cluster - by default, this will create a 3 node universe with a replication factor of 3.

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl create
```

## 2. Create a table with secondary indexes

Connect to the cluster using `cqlsh`.

```{.sh .copy .separator-dollar}
$ ./bin/cqlsh
```
```sh
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh>
```

Create a keyspace.

```{.sql .copy .separator-gt}
cqlsh> CREATE KEYSPACE store;
```

Create a table with the `transactions` property set to enabled.

```{.sql .copy .separator-gt}
cqlsh> CREATE TABLE store.orders (
  customer_id int,
  order_date timestamp,
  amount double,
  PRIMARY KEY ((customer_id), order_date)
) with transactions = { 'enabled' : true };
```

Now create a secondary index on the `order_date` column. Note that we add the `amount` column as a covering column in the secondary index in order to respond to queries selecting the `amount` column directly from the secondary index table with just one read.

```{.sql .copy .separator-gt}
cqlsh> create index orders_by_date on store.orders (order_date, customer_id) covering (amount);
```


## 3. Insert sample data

Let us seed this table with some sample data. Paste the following into the cqlsh prompt.

```{.sql .copy}
INSERT INTO store.orders (customer_id, order_date, amount) VALUES (1, '2018-04-02', 100.30);
INSERT INTO store.orders (customer_id, order_date, amount) VALUES (2, '2018-04-02', 50.45);
INSERT INTO store.orders (customer_id, order_date, amount) VALUES (1, '2018-04-06', 20.25);
INSERT INTO store.orders (customer_id, order_date, amount) VALUES (3, '2018-04-06', 200.80);
```


## 4. Perform some queries

- **Get the total amount for a customer**

Let us write a query to fetch the sum total of the order `amount` column across all orders for a customer. This query will be executed against the primary table using the partition key `customer_id`, and therefore does not use the secondary index.

```{.sql .copy .separator-gt}
cqlsh> select sum(amount) from store.orders where customer_id = 1;
```
```sql
 sum(amount)
-------------
      120.55
(1 rows)
```

- **Get the total amount for a specific date**

Now, let us write a query to fetch the sum total of order `amount` across all orders for a specific date. Because we have a secondary index on the `order_date` column of the table, the query analyzer will execute the query against the secondary index using the partition key `order_date` and avoid a full-table scan of the primary table.

```{.sql .copy .separator-gt}
cqlsh> select sum(amount) from store.orders where order_date = '2018-04-02';
```
```sql
 sum(amount)
-------------
      150.75
(1 rows)
```

## 5. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl destroy
```


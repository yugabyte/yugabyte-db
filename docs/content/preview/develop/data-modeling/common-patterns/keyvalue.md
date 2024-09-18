---
title: Key-value data model
headerTitle: Key-value data model
linkTitle: Key-value
description: Explore the key-value data model
headcontent: Explore the key-value data model
menu:
  preview:
    identifier: common-patterns-keyvalue
    parent: common-patterns
    weight: 200
type: docs
---

In the key-value data model, each key is associated with one and only one value. Key-value stores expose three basic APIs:

- `GET` to fetch the value of a key (for example, `GET('name')`)
- `SET` to store the value of a key (for example, `SET('name', 'yugabyte')`)
- `DEL` to delete a key and its value (for example, `DEL('name')`)

With these three simple functionalities, key-value stores have carved themselves a niche in modern infrastructure because of their speed and simplicity.

YugabyteDB provides several advantages when used as a key-value store:

- YugabyteDB internally [stores data](../../../../architecture/docdb/data-model/) as a collection of key-value pairs and therefore automatically excels as a key-value store.
- Being [distributed by design](../../../../architecture/transactions/distributed-txns/), YugabyteDB also naturally acts as a distributed key-value store.
- YugabyteDB inherently provides consistency of data because of Raft replication, which is typically not guaranteed by other key-value stores.

## Use cases

1. **Cache server** : The key-value data model is best suited for designing cache servers where the cached data is represented by a key. The cached object could be represented in JSON or Hstore (to have multiple attributes) and parsed by the application.

1. **Telephone directory** : A telephone directory instantly falls into the key-value model, where the key is the phone number and the value is the name and address of the person to whom the phone number belongs.

1. **Session store** : A session-oriented application, such as a web application, starts a session when a user logs in, and is active until the user logs out or the session times out. During this period, the application stores all session-related data like profile information, themes, zipcode, geography, and so on, in a fast key-value store.

1. **Shopping cart** : A user's shopping cart can be represented as a JSON or Hstore and stored under a key (for example, `user1.cart`). Given the strong consistency and resilience offered by YugabyteDB, the cart information will not be lost even in case of disasters.

## Simple scenario

Consider a scenario where you want to store multiple details related to users like `id`, `name`, `country`. For this, you could adopt a simple key-value schema where each attribute is a separate key, such as the following where the key contains both the `id` and the attribute name while the value is the value of the attribute, like:

```json{.nocopy}
user1.name = "John Wick"
user1.country = "USA"
user2.name = "Harry Potter"
user2.country = "UK"
```

The primary concern with the above schema is that the database will have to do multiple internal lookups to fetch the data for a single user as each attribute will be stored as a different row. To avoid this, you could choose to store the user data in an [HStore](https://www.postgresql.org/docs/11/hstore.html) type, like:

```json{.nocopy}
1 : {"name" : "John Wick", "country" : "USA"}
2 : {"name" : "Harry Potter", "country" : "UK"}
```

{{<note title="Note">}}
You could opt for the JSON type if you have a complex nested set of attributes. Hstore would be a better choice if the data is a set of simple key-value pairs
{{</note>}}

Let us go over both schemas.

## Cluster setup

{{<cluster-setup-tabs>}}

## Attributes as separate rows

Follow the steps below to set up your table.

1. Create the table.

    ```sql
    CREATE TABLE kvstore (
        key VARCHAR,
        value VARCHAR,
        PRIMARY KEY(key)
    );
    ```

1. Add some data.

    ```sql
    INSERT INTO kvstore VALUES ('user1.name', 'John Wick'), ('user1.country', 'USA'),
                              ('user2.name', 'Harry Potter'), ('user2.country', 'UK');
    ```

1. If you fetch the rows from the table as,

    ```sql
    SELECT * FROM kvstore;
    ```

    You will see the output to be:

    ```sql{.nocopy}
          key      |    value
    ---------------+--------------
    user2.country | UK
    user1.name    | John Wick
    user1.country | USA
    user2.name    | Harry Potter
    ```

### GET

To get the name of user1, you can execute the following:

```sql
-- GET('user1.name')
SELECT value FROM kvstore WHERE key = 'user1.name';
```

```output
   value
-----------
 John Wick
```

### SET

To store a value for a key, you can do an insert. Because the key could already exist, you should provide an `ON CONFLICT UPDATE` clause.

```sql
INSERT INTO kvstore(key, value) VALUES('user1.name', 'Jack Ryan')
        ON CONFLICT (key) DO
        UPDATE SET value = EXCLUDED.value;
```

### DEL

To delete a key and its value, you can execute a simple `DELETE` command as follows:

```sql
DELETE FROM kvstore WHERE key = 'user1.name';
```

## Attributes as one row

To store multiple attributes associated with a user as one entry, you can use the [Hstore](https://www.postgresql.org/docs/11/hstore.html) type. Follow the steps below to set up your table.

1. Create the HStore extension.

    ```sql
    CREATE EXTENSION hstore;
    ```

1. Create the table as follows.

    ```sql
    CREATE TABLE kvstore1 (
        id int,
        attributes hstore,
        PRIMARY KEY(id)
    );
    ```

1. Add some data.

    ```sql
    INSERT INTO kvstore1 VALUES (1, '"name" => "John Wick", "country" => "USA"'),
                                (2, '"name" => "Harry Potter", "country" => "UK"');
    ```

1. If you fetch the rows from the table as,

    ```sql
    SELECT * FROM kvstore1 ;
    ```

    You should see the following output.

    ```sql{.nocopy}
    id |               attributes
    ----+-----------------------------------------
      1 | "name"=>"John Wick", "country"=>"USA"
      2 | "name"=>"Harry Potter", "country"=>"UK"
    ```

### GET

To get the name of the user with `id=1`, you can execute:

```sql
SELECT attributes->'name' as name FROM kvstore1 WHERE id = 1;
```

```sql{.nocopy}
   name
-----------
 John Wick
```

### SET

To store a value for an attribute, you can do an insert. Because the key could already exist, you should provide an `ON CONFLICT UPDATE` clause.

```sql
INSERT INTO kvstore1(id, attributes) VALUES(1, '"name" => "John Malkovich"')
        ON CONFLICT (id) DO
        UPDATE SET attributes = kvstore1.attributes || EXCLUDED.attributes;
```

If you are sure that an entry for that user already exists, you could just do an update as:

```sql
UPDATE kvstore1 SET attributes = attributes || '"name" => "John Malkovich"' WHERE id = 1;
```

### DEL

To delete an attribute and its value, you can execute:

```sql
UPDATE kvstore1 SET attributes = delete(attributes, 'name') WHERE id = 1;
```

Now if you fetch the rows from the table as,

```sql
SELECT * FROM kvstore1;
```

you will notice that the `name` attribute has been removed for user `id=1`.

```sql{.nocopy}
 id |               attributes
----+-----------------------------------------
  1 | "country"=>"USA"
  2 | "name"=>"Harry Potter", "country"=>"UK"
```

## Learn more

- [Hstore](https://www.postgresql.org/docs/11/hstore.html)
- [Json](../../../../explore/ysql-language-features/jsonb-ysql/)
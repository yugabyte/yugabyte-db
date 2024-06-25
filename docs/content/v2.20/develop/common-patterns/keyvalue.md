---
title: Key-value data model
headerTitle: Key-value data model
linkTitle: Key-value
description: Explore the key-value data model
headcontent: Explore the key-value data model
menu:
  v2.20:
    identifier: common-patterns-keyvalue
    parent: common-patterns
    weight: 200
type: indexpage
---

In the key-value data model, each key is associated with one and only one value. Key-value stores expose three basic APIs:

- `GET` to fetch the value of a key (for example, `GET('name')`)
- `SET` to store the value of a key (for example, `SET('name', 'yugabyte')`)
- `DEL` to delete a key and its value (for example, `DEL('name')`)

With these three simple functionalities, key-value stores have carved themselves a niche in modern infrastructure because of their speed and simplicity.

YugabyteDB provides several advantages when used as a key-value store:

- YugabyteDB internally [stores data](../../../architecture/docdb/persistence/) as a collection of key-value pairs and therefore automatically excels as a key-value store.
- Being [distributed by design](../../../architecture/transactions/distributed-txns/), YugabyteDB also naturally acts as a distributed key-value store.
- YugabyteDB inherently provides consistency of data because of RAFT replication, which is typically not guaranteed by other key-value stores.

## Setup

{{<cluster-setup-tabs>}}

## Store user data

For example, to store the details of a user, you could adopt a schema where each attribute is a separate key, such as the following:

```json{.nocopy}
user1.name = "John Wick"
user1.country = "USA"
user2.name = "Harry Potter"
user2.country = "UK"
```

{{<note title="Note">}}You can also consider a schema where the value is a JSON structure consisting of multiple attributes associated with a user.{{</note>}}

For this, you can create a table as follows:

```sql
CREATE TABLE kvstore (
    key VARCHAR,
    value VARCHAR,
    PRIMARY KEY(key)
);
```

To add some data, enter the following:

```sql
INSERT INTO kvstore VALUES ('user1.name', 'John Wick'), ('user1.country', 'USA'),
                           ('user2.name', 'Harry Potter'), ('user2.country', 'UK');
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

## Use cases

### Cache server

The key-value data model is best suited for designing cache servers where the cached data is represented by a key. The cached object could be represented in JSON string (to have multiple attributes) and parsed by the application.

### Telephone directory

A telephone directory instantly falls into the key-value model, where the key is the phone number and the value is the name and address of the person that the phone number belongs to.

### Session store

A session-oriented application, such as a web application, starts a session when a user logs in, and is active until the user logs out or the session times out. During this period, the application stores all session-related data like profile information, themes, zipcode, geography, and so on, in a fast key-value store.

### Shopping cart

A user's shopping cart can be represented as a JSON string and stored under a key (for example, `user1.cart`). Given the strong consistency and resilience offered by YugabyteDB, the cart information will not be lost even in case of disasters.

---
title: Key-Value data model
headerTitle: Key-Value data model
linkTitle: Key-Value
description: Explore the Key-Value data model
headcontent: Explore the Key-Value data model
menu:
  preview:
    identifier: common-patterns-keyvalue
    parent: common-patterns
    weight: 200
type: indexpage
---

In the Key-value data model, each key is associated with one and only one value. YugabyteDB internally stores data as a collection of key-value pairs and hence automatically excels as a key-value store. Being distributed by design, YugabyteDB naturally acts as a Distributed Key-Value store. Let us explore the Key-Value data model with examples. YugabyteDB inherently provides consistency of data because of RAFT replication which is typically not guaranteed by other Key-Value stores.

## Functionality

Key-Value stores expose 3 basic apis.

- `GET` to fetch the value of a key (eg. `GET('name')`)
- `SET` to store the value of a key (eg. `PUT('name', 'yugabyte')`)
- `DEL` to delete a key and its value (eg. `DEL('name')`)

With just three simple functionalities, Key-Value stores have carved themselves a niche space in modern infrastructure because of their speed and simplicity.

## Setup

{{<cluster-setup-tabs>}}

## Storing user data

For example, to store the details of a user, you can choose to adopt a schema where each attribute is a separate key. For example,

```json{.nocopy}
user1.name = "John Wick"
user1.country = "USA"
user2.name = "Harry Potter"
user2.country = "UK"
```

{{<note title="Note">}} You can also consider a schema where the value is a JSON structure consisting of multiple attributes associated with a user{{</note>}}

For this, you can create a table as follows.

```sql
CREATE TABLE kvstore (
    key VARCHAR,
    value VARCHAR,
    PRIMARY KEY(key)
);
```

Let us add some data.

```sql
INSERT INTO kvstore VALUES ('user1.name', 'John Wick'), ('user1.country', 'USA'),
                           ('user2.name', 'Harry Potter'), ('user2.country', 'UK');
```

### GET

To get the name of user1, you can execute,

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

To store a value for a key, you can do an insert. But as the key could already exist, you should provide an `ON CONFLICT UPDATE` clause.

```sql
INSERT INTO kvstore(key, value) VALUES('user1.name', 'Jack Ryan') 
        ON CONFLICT (key) DO
        UPDATE SET value = EXCLUDED.value;
```

### DEL

To delete a key and its value, you can execute a simple `DELETE` command.

```sql
DELETE FROM kvstore WHERE key = 'user1.name';
```

## Use cases

### Cache server

The Key-Value model is best suited for designing cache servers where the cached data is represented by a key. The cached object could be represented in JSON string (to have multiple attributes) and parsed by the application.

### Telephone directory

A telephone directory instantly falls into the Key-Value model where the key is the phone number and the value is the name and address of the person that the phone number belongs to.

### Session store

A session-oriented application such as a web application starts a session when a user logs in and is active until the user logs out or the session times out. During this period, the application stores all session-related data like profile information, themes, zipcode, geo etc in a fast Key-Value store. 

### Shopping cart

The whole shopping cart for a user could be represented as a JSON string and stored under a key (eg. `user1.cart`). Given the strong consistency and resilience offered by YugabyteDB, the cart information will not be lost even in case of disasters.

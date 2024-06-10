---
title: DocDB data model
headerTitle: DocDB data model
linkTitle: Data model
description: Understand how DocDB stores table rows as keys and values
headcontent: Understand how DocDB stores table rows as keys and values
aliases:
  - /stable/architecture/docdb/persistence/
menu:
  stable:
    identifier: docdb-data-model
    parent: docdb
    weight: 100
type: docs
---

DocDB is a document-oriented data store that uses a key-value model for persisting and retrieving data. In this approach, data is stored as values associated with unique keys. Each row in a table is represented as a document in DocDB. The data store supports ordered operations on these documents, enabling efficient range queries and iterative access through keys. The keys themselves are designed to facilitate rapid lookups and optimized range scanning over the stored values.

To better understand DocDB's architecture, let's examine how the keys and values are structured in the system.

![Data model](/images/architecture/docdb_key_value_encoding.png)

{{<note>}}
DocDB has adopted an optimized form of row storage as of v2.20. For details, see [Packed rows](../packed-rows).
{{</note>}}

## DocDB key

The keys in a DocDB document model are compound keys consisting of zero or more hash-organized components followed by zero or more ordered (range) components. These components are stored in their data type-specific sort order, with both ascending and descending sort order supported for each ordered component of the key.  If any hash columns are present then they are preceded by a 16-bit hash of the hash column values.

If [colocation](../../docdb-sharding/colocated-tables/) is used, then the key is prefixed with the colocation ID of the table it is referring to (not shown in the diagram); this separates data from different tables colocated in the same tablet.

## DocDB value

The values in a DocDB document data model can be of the following types:

- Primitive types, such as int32, int64, double, text, timestamp, and so on.
- Non-primitive types (sorted maps), where objects map scalar keys to values that could be either scalar or sorted maps.

This model allows multiple levels of nesting and corresponds to a JSON-like format. Other data structures such as lists, sorted sets, and so on are implemented using DocDB's object type with special key encodings. In DocDB, [hybrid timestamps](../../transactions/transactions-overview/#hybrid-logical-clocks) of each update are recorded carefully, making it possible to recover the state of any document at some point in the past. Overwritten or deleted versions of data are garbage-collected as soon as there are no transactions reading at a snapshot at which the old value would be visible.

## Encoding documents

In DocDB, each table row is converted to multiple key-value pairs along with timestamps. Because documents are spread across many different key-values, it is possible to partially modify them without incurring overhead.

The following example shows a document stored in DocDB:

```text
DocumentKey1 = {
  SubKey1 = {
    SubKey2 = Value1
    SubKey3 = Value2
  },
  SubKey4 = Value3
}
```

Keys stored in DocDB consist of a number of components, where the first component is a document key, followed by several scalar components, and finally followed by a MVCC timestamp (sorted in reverse order). Each component in the DocumentKey, SubKey, and Value, are "PrimitiveValues", which are type-value pairs that can be encoded to and decoded from byte arrays. When encoding primitive values in keys, a binary-comparable encoding is used for the value, so that sort order of the encoding is the same as the sort order of the value.

## Updates and deletes

Suppose the document provided in the example in [Encoding documents](#encoding-documents) was written at time `T10` entirely, then internally that document is stored using five DocDB key value pairs, as per the following:

```output
DocumentKey1, T10 -> {} // This is an init marker
DocumentKey1, SubKey1, T10 -> {}
DocumentKey1, SubKey1, SubKey2, T10 -> Value1
DocumentKey1, SubKey1, SubKey3, T10 -> Value2
DocumentKey1, SubKey4, T10 -> Value3
```

Deletions of documents and sub-documents are performed by writing a single Tombstone marker at the corresponding value. During compaction, overwritten or deleted values are cleaned up to reclaim space.

## Primary key columns

The document key contains the full primary key with column values organized in the following order:

1. A 16-bit hash of the hash column values is stored if any hash columns are present.
2. The hash columns are stored.
3. The clustering (range) columns are stored.

Each data type supported is represented by a unique byte. The type prefix is also present in the primary key hash or range components.

{{<note>}}
In the scenario where a primary key is not explicitly defined, YugabyteDB automatically generates an internal row ID to uniquely identify the row.
{{</note>}}

## Non-primary key columns

The non-primary key columns correspond to sub-documents in the document. The sub-document key corresponds to the column ID. There's a unique byte for each data type Yugabyte supports. The values are prefixed with the corresponding byte. If a column is a non-primitive type (such as a map or set), the corresponding sub-document is an object.

A binary-comparable encoding is used for translating the value for each type to strings that are added to the key-value store.

## Examples

Consider the following table schema:

```sql
CREATE TABLE msgs (user_id text,
                   msg_id int,
                   msg text,
                   msg_props map<text, text>,
      PRIMARY KEY ((user_id), msg_id));
```

### Insert a row

```sql
T1: INSERT INTO msgs (user_id, msg_id, msg, msg_props)
      VALUES ('user1', 10, 'msg1', {'from' : 'a@b.com', 'subject' : 'hello'});
```

The entries in DocDB would look similar to the following:

```output.sql
(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'
(hash1, 'user1', 10), msg_props_column_id, 'from', T1 -> 'a@b.com'
(hash1, 'user1', 10), msg_props_column_id, 'subject', T1 -> 'hello'
```

### Update subset of columns

The following example updates a subset of columns:

```sql
T2: UPDATE msgs
       SET msg_props = msg_props + {'read_status' : 'true'}
     WHERE user_id = 'user1', msg_id = 10
```

The entries in DocDB would look similar to the following:

<pre>
<code>(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'
(hash1, 'user1', 10), msg_props_column_id, 'from', T1 -> 'a@b.com'
<b>(hash1, 'user1', 10), msg_props_column_id, 'read_status', T2 -> 'true'</b>
(hash1, 'user1', 10), msg_props_column_id, 'subject', T1 -> 'hello'
</code>
</pre>

### Update entire row

The following example updates an entire row:

```sql
T3: INSERT INTO msgs (user_id, msg_id, msg, msg_props)
        VALUES (‘user1’, 20, 'msg2', {'from' : 'c@d.com', 'subject' : 'bar'});
```

The entries in DocDB would look similar to the following:

<pre>
<code>(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'
(hash1, 'user1', 10), msg_props_column_id, 'from', T1 -> 'a@b.com'
(hash1, 'user1', 10), msg_props_column_id, 'read_status', T2 -> 'true'
(hash1, 'user1', 10), msg_props_column_id, 'subject', T1 -> 'hello'
<b>(hash1, 'user1', 20), liveness_column_id, T3 -> [NULL]
(hash1, 'user1', 20), msg_column_id, T3 -> 'msg2'
(hash1, 'user1', 20), msg_props_column_id, 'from', T3 -> 'c@d.com'
(hash1, 'user1', 20), msg_props_column_id, 'subject', T3 -> 'bar'</b></code>
</pre>

### Delete a row

The following example deletes a single column from a row:

```sql
T4: DELETE msg_props
      FROM msgs
     WHERE user_id = 'user1'
       AND msg_id = 10;
```

Even though in the preceding example the column being deleted is a non-primitive column (a map), this
operation only involves adding a delete marker at the correct level, and does not incur any read overhead. The logical layout in DocDB at this point should be similar to the following:

<pre>
<code>(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'
<b>(hash1, 'user1', 10), msg_props_column_id, T4 -> [DELETE]</b>
<s>(hash1, 'user1', 10), msg_props_column_id, 'from', T1 -> 'a@b.com'
(hash1, 'user1', 10), msg_props_column_id, 'read_status', T2 -> 'true'
(hash1, 'user1', 10), msg_props_column_id, 'subject', T1 -> 'hello'</s>
(hash1, 'user1', 20), liveness_column_id, T3 -> [NULL]
(hash1, 'user1', 20), msg_column_id, T3 -> 'msg2'
(hash1, 'user1', 20), msg_props_column_id, 'from', T3 -> 'c@d.com'
(hash1, 'user1', 20), msg_props_column_id, 'subject', T3 -> 'bar'</code>
</pre>
The key-value pairs that are displayed in strike-through font are logically deleted. The preceding DocDB layout is not the physical layout, as the writes happen in a log-structured manner.

When compactions occur, the space for the key-value pairs corresponding to the deleted columns is reclaimed, as per the following:

<pre>
<code>(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'
(hash1, 'user1', 20), liveness_column_id, T3 -> [NULL]
(hash1, 'user1', 20), msg_column_id, T3 -> 'msg2'
(hash1, 'user1', 20), msg_props_column_id, 'from', T3 -> 'c@d.com'
(hash1, 'user1', 20), msg_props_column_id, 'subject', T3 -> 'bar'
T5: DELETE FROM msgs    // Delete entire row corresponding to msg_id 10
     WHERE user_id = 'user1'
            AND msg_id = 10;

<b>(hash1, 'user1', 10), T5 -> [DELETE]</b>
<s>(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'</s>
(hash1, 'user1', 20), liveness_column_id, T3 -> [NULL]
(hash1, 'user1', 20), msg_column_id, T3 -> 'msg2'
(hash1, 'user1', 20), msg_props_column_id, 'from', T3 -> 'c@d.com'
(hash1, 'user1', 20), msg_props_column_id, 'subject', T3 -> 'bar'</code>
</pre>

## Data expiration

{{<warning>}}
Data expiration is a [YCQL](../../../api/ycql)-only feature.
{{</warning>}}

YCQL supports expiration of rows. The expiration is specified as TTL (Time-to-live) value in a few ways.

- Table-level TTL: YCQL allows the TTL property to be specified at the table level. In this case, TTL is not stored on a per key-value pair basis in DocDB; instead, TTL is implicitly enforced on reads and during compactions to reclaim space.
- Row- and column-level TTL: YCQL allows the TTL property to be specified at the level of each `INSERT` and `UPDATE` operation. In such cases, TTL is stored as part of the DocDB value.

If no TTL is present at the column's value, the table TTL acts as the default value.

YCQL has a distinction between rows created using Insert vs Update. YugabyteDB keeps track of this difference (and row-level TTLs) using a "liveness column", a special system column invisible to the user. It is added for inserts, but not updates, which ensures the row is present even if all non-primary key columns are deleted only in the case of inserts.

The following example demonstrates use of the row-level TTL:

```sql
CREATE TABLE page_views (page_id text,
                         views int,
                         category text,
     PRIMARY KEY ((page_id)));
```

### Insert row with TTL

The following example inserts a row with TTL:

```sql
T1: INSERT INTO page_views (page_id, views)
        VALUES ('abc.com', 10)
        USING TTL 86400
```

The entries in DocDB should look similar to the following:

<pre>
<code>(hash1, 'abc.com'), liveness_column_id, T1 -> (TTL = 86400) [NULL]
(hash1, 'abc.com'), views_column_id, T1 -> (TTL = 86400) 10
</code>
</pre>

### Update row with TTL

The following example updates a row with TTL:

```sql
T2: UPDATE page_views
     USING TTL 3600
       SET category = 'news'
     WHERE page_id = 'abc.com';
```

The entries in DocDB should look similar to the following:

<pre>
<code>(hash1, 'abc.com'), liveness_column_id, T1 -> (TTL = 86400) [NULL]
(hash1, 'abc.com'), views_column_id, T1 -> (TTL = 86400) 10
<b>(hash1, 'abc.com'), category_column_id, T2 -> (TTL = 3600) 'news'</b>
</code>
</pre>

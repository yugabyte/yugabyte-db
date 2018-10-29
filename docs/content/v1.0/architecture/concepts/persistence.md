---
title: Persistence
linkTitle: Persistence
description: Data Persistence with DocDB
menu:
  v1.0:
    identifier: architecture-persistence
    parent: architecture-concepts
    weight: 960
---

## Introduction

DocDB is YugaByte’s Log Structured Merge tree (LSM) based storage engine. Once data is replicated
via Raft  across a majority of the tablet-peers, it is applied to each tablet peer’s local DocDB.

DocDB is a persistent “**key to object/document**” store rather than just a “key to value” store.

* The **keys** in DocDB are compound keys consisting of:
  * 1 or more **hash organized components**, followed by
  * 0 or more **ordered (range) components**. These components are stored in their data type specific sort order; both
    ascending and descending sort order is supported for each ordered component of the key.
* The **values** in DocDB can be:
  * **primitive types**: such as int32, int64, double, text, timestamp, etc.
  * **object types (sorted maps)**: These objects map scalar keys to values, which could be either scalar or sorted maps as well.

This model allows multiple levels of nesting, and corresponds to a JSON-like format. Other data
structures like lists, sorted sets etc. are implemented using DocDB’s object type with special key
encodings. In DocDB, [hybrid timestamps](../../transactions/distributed-txns/)
of each update are recorded carefully, so that it is possible to
recover the state of any document at some point in the past. Overwritten or deleted versions
of data are garbage-collected as soon as there are no transactions reading at a snapshot at which
the old value would be visible.

YugaByte’s DocDB uses a highly customized version of [RocksDB](http://rocksdb.org/), a
log-structured merge tree (LSM) based key-value store. The primary motivation behind the
enhancements or customizations to RocksDB are described below:

* **Efficient implementation of a row/document model on top of a KV store**: To implement a flexible data
  model---such as a row (comprising of columns), or collections types (such as list, map, set) with
  arbitrary nesting---on top of a key-value store, but, more importantly, to implement efficient
  operations on this data model such as:
  * fine-grained updates to a part of the row or collection without incurring a read-modify-write
    penalty of the entire row or collection
  * deleting/overwriting a row or collection/object at an arbitrary nesting level without incurring a
    read penalty to determine what specific set of KVs need to be deleted
  * enforcing row/object level TTL based expiry
  a tighter coupling into the “read/compaction” layers of the underlying KV store (RocksDB) is needed.
  We use RocksDB as an append-only store and operations such as row or collection delete are modeled
  as an insert of a special “delete marker”.  This allows deleting an entire subdocument efficiently
  by just adding one key/value pair to RocksDB. Read hooks automatically recognize these markers and
  suppress expired data. Expired values within the subdocument are cleaned up/garbage collected by our
  customized compaction hooks.

* **Avoid penalty of two transaction/write-ahead (WAL) logs**: YugaByte is a distributed database that
  uses Raft for replication. Changes to the distributed system are already recorded/journalled as part
  of Raft logs. When a change is accepted by a majority of peers, it is applied to each tablet peer’s
  DocDB, but the additional WAL mechanism in RocksDB (under DocDB) is unnecessary and adds overhead.
  For correctness, in addition to disabling the WAL mechanism in RocksDB, YugaByte tracks the Raft
  “sequence id” up to which data has been flushed from RocksDB’s memtables to SSTable files. This
  ensures that we can correctly garbage collect the Raft WAL logs as well as replay the minimal number
  of records from Raft WAL logs on a server crash or restart.

* **Multi-version concurrency control (MVCC) is done at a higher layer**: The mutations to records in the
  system are versioned using hybrid-timestamps maintained at the YBase layer. As a result, the notion
  of MVCC as implemented in a vanilla RocksDB (using sequence ids) is not necessary and only adds
  overhead. YugaByte does not use RocksDB’s sequence ids, and instead uses hybrid-timestamps that are
  part of the encoded key to implement MVCC.

* **Backups/Snapshots**: These need to be higher level operations that take into consideration data in
  DocDB as well as in the Raft logs to get a consistent cut of the state of the system

* **Read Optimizations**:
  * **Data model aware bloom filters**: The keys stored by DocDB in RocksDB consist of a number of
  components, where the first component is a "document key", followed by a few scalar components, and
  finally followed by a timestamp (sorted in reverse order).

      * The bloom filter needs to be aware of what components of the key need be added to the bloom so that only the relevant SSTable files in the LSM store are looked up during a read operation.

      * In a traditional KV store, range scans do not make use of bloom filters because exact keys that fall in the range are unknown. However, we have implemented a data-model aware bloom filter, where range scans within keys that share the same hash component can also benefit from bloom filters. For
      example, a scan to get all the columns within row or all the elements of a collection can also
      benefit from bloom filters.

  * **Data model aware range query optimization**: The ordered (or range) components of the compound-keys in DocDB frequently have a natural order. For example, it may be an int that represents a message id
  (for a messaging like application) or a timestamp (for a IoT/Timeseries like use case). See example
  below. By keeping hints with each SSTable file in the LSM store about the min/max values for these
  components of the “key”, range queries can intelligently prune away the lookup of irrelevant SSTable
  files during the read operation.

    ```sql
        SELECT message_txt
          FROM messages
        WHERE user_id = 17
          AND message_id > 50
          AND message_id < 100;
    ```

    Or, in the context of a time-series application:
    ```sql
        SELECT metric_value
          FROM metrics
        WHERE metric_name = ’system.cpu’
          AND metric_timestamp < ?
          AND metric_timestamp > ?
    ```

* **Server-global block cache across multiple DocDB instances**: A shared block cache is used across the
DocDB/RocksDB instances of all the tablets hosted by a YB-TServer. This maximizes the use of memory
resources, and avoids creating silos of cache that each need to be sized accurately for different
user tables.

* **Server-global memstore limits across multiple DocDB instances**: While per-memstore flush sizes can be
configured, in practice, because the number of memstores may change over time as users create new
tables, or tablets of a table move between servers, we have enhanced the storage engine to enforce a
global memstore threshold. When such a threshold is reached, selection of which memstore to flush
takes into account what memstores carry the oldest records (determined using hybrid timestamps) and
therefore are holding up Raft logs and preventing them from being garbage collected.

* **Scan-resistant block cache**: We have enhanced RocksDB’s block cache to be scan resistant. The
motivation was to prevent operations such as long-running scans (e.g., due to an occasional large
query or background Spark jobs) from polluting the entire cache with poor quality data and wiping
out useful/hot data.

## Encoding Details

DocDB is the storage layer that acts as the common backbone of different APIs that are supported by
YugaByte (currently CQL, Redis, and PostgreSQL(beta)).

### Mapping Documents to Key-Value Store

The documents are stored using a key-value store based on RocksDB, which is typeless. The documents
are converted to multiple key-value pairs along with timestamps. Because documents are spread across
many different key-values, it’s possible to partially modify them cheaply.

For example, consider the following document stored in DocDB:

```
DocumentKey1 = {
	SubKey1 = {
		SubKey2 = Value1
		SubKey3 = Value2
	},
	SubKey4 = Value3
}
```

Keys we store in RocksDB consist of a number of components, where the first component is a "document
key", followed by a few scalar components, and finally followed by a MVCC timestamp (sorted in
reverse order). Each component in the DocumentKey, SubKey, and Value, are PrimitiveValues, which are
just (type, value) pairs, which can be encoded to and decoded from strings. When we encode primitive
values in keys, we use a binary-comparable encoding for the value, so that sort order of the
encoding is the same as the sort order of the value.

Assume that the example document above was written at time T10 entirely. Internally the above
example’s document is stored using 5 RocksDB key value pairs:

```
DocumentKey1, T10 -> {} // This is an init marker
DocumentKey1, SubKey1, T10 -> {}
DocumentKey1, SubKey1, SubKey2, T10 -> Value1
DocumentKey1, SubKey1, SubKey3, T10 -> Value2
DocumentKey1, SubKey4, T10 -> Value3
```

Deletions of Documents and SubDocuments are performed by writing a single Tombstone marker at the
corresponding value. During compaction, overwritten or deleted values are cleaned up to reclaim
space.

### Mapping of CQL rows to Documents

For CQL tables, every row is a document in DocDB. The Document key contains the full primary key -
the values of partition (hash) column(s) and clustering (range) column(s), in order. A 16-bit hash
of the hash portion is also prefixed in the DocKey. The subdocuments within the Document are the
rest of the columns, whose SubKey is the corresponding column ID. If a column is a non-primitive
type (such as a map or set), the corresponding subdocument is an Object.

There’s a unique byte for each data type we support in CQL. The values are prefixed with the
corresponding byte. The type prefix is also present in the primary key’s hash or range components.
We use a binary-comparable encoding to translate the value for each CQL type to strings that go to
the KV-Store.

In CQL there are two types of TTL, the table TTL and column level TTL. The column TTLs are stored
with the value using the same encoding as Redis. The Table TTL is not stored in DocDB (it is stored
in master’s syscatalog as part of the table’s schema). If no TTL is present at the column’s value,
the table TTL acts as the default value.

Furthermore, CQL has a distinction between rows created using Insert vs Update. We keep track of
this difference (and row level TTLs) using a "liveness column", a special system column invisible to
the user. It is added for inserts, but not updates: making sure the row is present even if all
non-primary key columns are deleted only in the case of inserts.

![cql_row_encoding](/images/architecture/cql_row_encoding.png)

**CQL Example: Rows with primitive and collection types**

Consider the following CQL commands:

```sql
CREATE TABLE msgs (user_id text,
                   msg_id int,
                   msg text,
                   msg_props map<text, text>,
      PRIMARY KEY ((user_id), msg_id));

T1: INSERT INTO msgs (user_id, msg_id, msg, msg_props)
        VALUES ('user1', 10, 'msg1', {'from' : 'a@b.com', 'subject' : 'hello'});
```

The entries in DocDB at this point will look like the following:

```
(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'
(hash1, 'user1', 10), msg_props_column_id, 'from', T1 -> 'a@b.com'
(hash1, 'user1', 10), msg_props_column_id, 'subject', T1 -> 'hello'
```

```sql
T2: UPDATE msgs
       SET msg_props = msg_props + {'read_status' : 'true'}
     WHERE user_id = 'user1', msg_id = 10
```

The entries in DocDB at this point will look like the following:

<pre>
<code>(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'
(hash1, 'user1', 10), msg_props_column_id, 'from', T1 -> 'a@b.com'
<b>(hash1, 'user1', 10), msg_props_column_id, 'read_status', T2 -> 'true'</b>
(hash1, 'user1', 10), msg_props_column_id, 'subject', T1 -> 'hello'
</code>
</pre>

```sql
T3: INSERT INTO msgs (user_id, msg_id, msg, msg_props)
        VALUES (‘user1’, 20, 'msg2', {'from' : 'c@d.com', 'subject' : 'bar'});
```

The entries in DocDB at this point will look like the following:

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

```sql
T4: DELETE msg_props       // Delete a single column from a row
      FROM msgs
     WHERE user_id = 'user1'
       AND msg_id = 10;
```

Even though, in the example above, the column being deleted is a non-primitive column (a map), this
operation only involves adding a delete marker at the correct level, and does not incur any read
overhead. The logical layout in DocDB at this point is shown below.

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
*Note: The KVs that are displayed in “strike-through” font are logically deleted.*

Note: The above is not the physical layout per se, as the writes happen in a log-structured manner.
When compactions happen, the space for the KVs corresponding to the deleted columns is reclaimed, as
shown below.

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

**CQL Example #2: Time-To-Live (TTL) Handling**

CQL allows the TTL property to be specified at the level of each INSERT/UPDATE operation. In such
cases, the TTL is stored as part of the RocksDB value as shown below:

<pre>
<code>CREATE TABLE page_views (page_id text,
                         views int,
                         category text,
     PRIMARY KEY ((page_id)));

T1: INSERT INTO page_views (page_id, views)
        VALUES ('abc.com', 10)
        USING TTL 86400

// The entries in DocDB will look like the following

(hash1, 'abc.com'), liveness_column_id, T1 -> (TTL = 86400) [NULL]
(hash1, 'abc.com'), views_column_id, T1 -> (TTL = 86400) 10

T2: UPDATE page_views
     USING TTL 3600
       SET category = 'news'
     WHERE page_id = 'abc.com';

// The entries in DocDB will look like the following

(hash1, 'abc.com'), liveness_column_id, T1 -> (TTL = 86400) [NULL]
(hash1, 'abc.com'), views_column_id, T1 -> (TTL = 86400) 10
<b>(hash1, 'abc.com'), category_column_id, T2 -> (TTL = 3600) 'news'</b></code>
</pre>

**Table Level TTL**: CQL also allows the TTL property to be specified at the table level. In that case,
we do not store the TTL on a per KV basis in RocksDB; but the TTL is implicitly enforced on reads as
well as during compactions (to reclaim space).

### Mapping Redis Data to Documents

Redis is a schemaless data store. There is only one primitive type (string) and some collection
types. In this case, the documents are pretty simple. For primitive values, the document consists of
only one value. The document key is just a string prefixed with a hash. Redis collections are single
level documents. Maps correspond to SubDocuments which are discussed above. Sets are stored as maps
with empty values, and Lists have indexes as keys. For non-primitive values (e.g., hash, set type),
we store the type in parent level initial value, which is sorted before the subkeys. Any redis value
can have a TTL, which is stored in the RocksDB-value.

![redis_docdb_overview](/images/architecture/redis_docdb_overview.png)

**Redis Example**

| Timestamp | Command                                   | New Key-Value pairs added in RocksDB                                                                            |
|:---------:|:-----------------------------------------:|:---------------------------------------------------------------------------------------------------------------:|
|T1         |SET key1 value1 EX 15                      |(h1, key1), T1 -> 15, value1                                                                                     |
|T2         |HSET key2 subkey1 value1                   |(h2, key2), T2 -> [Redis-Hash-Type]<br/>(h2, key2), subkey1, T2 -> value1                                        |
|T3         |HSET key2 subkey2 value2                   |(h2, key2), subkey2, T3 -> value2                                                                                |
|T4         |DEL key2                                   |(h2, key2), T4 -> Tombstone                                                                                      |
|T5         |HMSET key2 subkey1 new_val1 subkey3 value3 |(h2, key2), T2 -> [Redis-Hash-Type]<br/>(h2, key2), subkey1, T5 -> new_val1<br/>(h2, key2), subkey3, T5 -> value3|
|T6         |SADD key3 value4 value5                    |(h3, key3), T6 -> [Redis-Set-Type]<br/>(h3, key3), value4, T6 -> [NULL]<br/>(h3, key3), value5, T6 -> [NULL]     |
|T7         |SADD key3 value6                           |(h3, key3), value6, T7 -> [NULL]                                                                                 |

Although they are added out of order, we get a sorted view of the items in the key value store when
reading, as shown below:

```
(h1, key1), T1 -> 15, value1
(h2, key2), T5 -> [Redis-Hash-Type]
(h2, key2), T4 -> Tombstone
(h2, key2), T2 -> [Redis-Hash-Type]
(h2, key2), subkey1, T5 -> new_val1
(h2, key2), subkey1, T2 -> value1
(h2, key2), subkey2, T3 -> value2
(h2, key2), subkey3, T5 -> value3
(h3, key3), T6 -> [Redis-Set-Type]
(h3, key3), value6, T7 -> [NULL]
(h3, key3), value4, T6 -> [NULL]
(h3, key3), value5, T6 -> [NULL]
```

Using an iterator, it is easy to reconstruct the hash and set contents efficiently.

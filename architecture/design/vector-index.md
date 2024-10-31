# Vector indexing architecture in YugabyteDB

## Introduction

YugabyteDB implements approximate nearest neighbor vector indexing and search
by combining the SQL interface of the pgvector PostgreSQL extension with a new
framework allowing to integrate existing vector indexing libraries at the
storage layer. Initial support is planned for the HNSW (Hierarchical Navigable
Small World) algorithm through [Usearch](https://github.com/unum-cloud/usearch)
and [Hnswlib](https://github.com/nmslib/hnswlib) libraries, with more
algorithms and libraries to be added later. We use these existing libraries to
generate indexes in memory and save them to disk. In the read path, we combine
query results from in-memory and immutable on-disk indexes and return the
aggregated top K results. This approach of maintaining multiple immutable
indexes on disk is inspired by LSM (Log-Structured Merge tree) storage engines
such as RocksDB, so we use the name **Vector LSM** for this new storage
subsystem. In the rest of this document, we describe Vector LSM and the overall
vector indexing and search architecture in YugabyteDB in detail.

## Architecture overview

The figure below shows the high-level architecture of the vector index
implementation in YugabyteDB. This diagram focuses on a single tablet replica's
storage subsystem for a vector index and omits the complexity of Raft
replication and cross-shard transactions.

<img
  src="https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/architecture/images/vector-index-architecture.svg"
  align="center"
  alt="YugabyteDB vector index architecture"/>

As the user inserts rows into a table for which a vector index exists, the new
data gets Raft-replicated, as usual. Assuming the Raft replication is done,
imagine that we are about to apply the new record to the underlying DocDB
storage system. In the absence of vector indexes, the vector data would only be
written to regular RocksDB (the RocksDB instance for transactionally committed
data). With a vector index present, the vector part of the data is also written
to Vector LSM, along with a unique identifier that allows us to find the
original row later.

Inside Vector LSM, the newly written vectors are accumulated in an in-memory
buffer, and are asynchronously inserted into a mutable in-memory vector index,
maintained by the corresponding library, using multiple threads in the
background. When the mutable in-memory vector index fills up, it gets persisted
to a file on disk using the serialization mechanism of the underlying library.

On the read path, we combine top K search results for the given query from the
following sources:

- The current mutable in-memory index

- Immutable in-memory indexes currently being written to disk

- Immutable on-disk index files, accessed via memory mapped files. The memory
mapped file approach could eventually be replaced with a custom cache, similar
to RocksDB block cache, for better control of the eviction policy.

In addition to the above, we also perform brute-force search of the following
sets of vectors, as these sets are not organized as vector indexes:

- In-memory buffers of Vector LSM (a simple list of vectors)

- Intents of commited transactions modifying vector columns

All of these results have to be filtered according to MVCC criteria, skipping
entries that have been deleted as well as those that are too new in relation to
the read_time of the request. This filtering could be partially pushed down as
a predicate to the vector indexing library, and partially done outside of it.

As a periodic maintenance procedure, the equivalent of a RocksDB compaction
merges multiple persistent vector indexes together, while simultaneously
discarding permanently deleted records. This is needed to avoid unbounded
growth of the number of vector index files in the steady state, and to reclaim
disk space occupied by deleted data. As a special case of a compaction, we also
want to support filtering a single index file to remove deleted data.

The diagram below illustrates the RPC flow for the write path and the read path.

<img
  src="https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/architecture/images/vector-index-flows.png"
  align="center"
  alt="YugabyteDB vector index RPC flow"/>

## Reusing the pgvector interface

We reuse the popular pgvector PostgreQSL extension for to provide a familiar
SQL interface to the user. However, the data storage engine for vector indexes
in YugabyteDB is completely different from that provided by pgvector.

A minimal syntax example from the pgvector documentation is given below. This
omits the step of installing the pgvector extension, which should be done
automatically in YugabyteDB.

```sql
CREATE TABLE items (id bigserial PRIMARY KEY, embedding vector(3));
INSERT INTO items (embedding) VALUES ('[1,2,3]'), ('[4,5,6]');
SELECT * FROM items ORDER BY embedding <-> '[3,1,2]' LIMIT 5;
```

The `<->` operator means the L2 distance. The above example does not yet use
any index, because none has been created, and the retrieval of nearest
neighbors has to be done via a full scan of all vectors in the table.

A more realistic example of a schema, perhaps used by a RAG
(retrieval-augmented generation) application, might look like the following.
This time we also include a `document_content` field that could be directly
useful to the application, perhaps as an input to an LLM. Application queries
would find vectors closest to the given query and retrieve document content
corresponding to those matching vectors.

```sql
CREATE TABLE documents (
    id BIGSERIAL PRIMARY KEY,
    document_content TEXT,
    embedding VECTOR(1536)
);
```

A vector index could be created on the above table with a syntax similar to the
following:
```
CREATE INDEX ON documents USING hnsw (
    embedding vector_l2_ops
) WITH (m = 16, ef_construction = 64);
```

The `ef` and `ef_construction` parameters are the configuration parameters of
the HNSW algorithm, and they are persistently stored in the index
configuration. `vector_l2_ops` specifies the type of distance to use. The above
index will speed up SELECT operations where results are ordered by the `<->`
operator.

Once the index is created, queries such as the following will utilize the index
to speed up retrieval, which would be necessary when the number of documents
is large, e.g. millions to billions.

```sql
SELECT id, document_content FROM documents ORDER BY embedding <-> $1 LIMIT $2;
```

Here, `$1` is the query vector and `$2` is the number of nearest results to
return.

## Copartitioning the vector index with the indexed table

In the above example, the `documents` table might contain a large number of
rows, and it might consist of multiple tablets, either created upfront during
table creation, or as a result of automatic tablet splitting. There are
multiple ways the vector index could be stored in relation to the indexed table.
We chose to store the vector index in the same DocDB table as the indexed
vector data, and partition it the same way as the indexed data. This approach
is known as "copartitioning". Its advantage over the alternative is improved
read performance: we can query the vector index and then immediately query
other data stored in the same tablet with the vector index using the keys
retrieved from the index, all as part of of the same RPC to the tablet. The
alternative approach, involving storing the vector index in a separate DocDB
tablet, was also carefully considered in our design process but ultimately not
pursued.

<img
  src="https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/architecture/images/vector-index-copartitioning-diagram.svg"
  align="center"
  alt="Co-partitioning vector indexes with the indexed table" />

Because the vector index data is stored next to the data of the main table, it
is sharded and replicated the same way as that table. The table has a primary
key different from the vector column. (As of this writing, we don't support
vector columns as primary keys.) Therefore, any vector can end up on any
tablet, based on the primary key, via the usual mechanism of hash or range
sharding. This means that read requests have to query all the tablets for the K
vectors nearest to the particular query vector, and then aggregate the results
to produce the final K results.

Note: in theory, it might be possible to implement vector-aware sharding
schemes using various clustering techniques and reduce the fan-out on the read
path. This more complicated design is out of scope of this document.

## Vector LSM internals

As a reminder, YugabyteDB stores data for relational tables and indexes in
DocDB. DocDB's per-node storage is based on RocksDB and stores data as
key-value pairs organized by the primary key, with hybrid timestamps for
multiversion concurrency control. For the schema of the `documents` table for a
RAG application discussed earlier, the contents of the RocksDB part of DocDB
might look as follows after a few insertion, update, and deletion operations:

```
id1, document_content_column_id, t10 -> content1
id1, embedding_column_id, t10        -> vector1
id2, t40                             -> <DEL>
id2, document_content_column_id, t20 -> content2
id2, embedding_column_id, t30        -> vector3
id2, embedding_column_id, t20        -> vector2
```

Here is the table of schema for reference:

```sql
CREATE TABLE documents (
    id BIGSERIAL PRIMARY KEY,
    document_content TEXT,
    embedding VECTOR(1536)
);
```

Because RocksDB is sorted by the primary key and not by the vector column, it
is clear that it would be very difficult to store the vector index in RocksDB
alone. **Vector LSM** is the subsystem utilizing state-of-the-art vector
indexing libraries. We built Vector LSM to serve as a counterpart of RocksDB on
each replica of a tablet, specialized for vector index data.

We add vectors to Vector LSM system on the write path at the same time as we
write them to regular RocksDB. Vector LSM abstracts away the concrete vector
indexing library and exports the following interface:

- Add a vector.

  - **Input:** (vector_id, vector, hybrid_time).

- Search for vectors.

    - **Input:** a query_vector and the number of results to return (k).
    Also, algorithm-specific parameters, such as ef (expansion factor) for HNSW.

    - **Returns:** k approximate nearest neighbors as pairs of (vector_id,
    distance).

A **vector_id** is an internal identifier that identifies a particular vector
within the Vector LSM. As of the time of this writing, we use random UUIDs for
vector ids.

On the read path, we query the indexes and merge the results from multiple
indexes (the in-memory buffer of vectors not yet written to any index, the
mutable index currently being written, all immutable indexes). We then map
internal vectors identifiers returned by index searches back to primary keys of
the indexed table (the id column in this example) and finally retrieve other
user-requested columns from the table by these relevant ids.

The user might create multiple vector indexes on multiple vector columns in the
same table, or even multiple vector indexes (perhaps with different parameters)
on the same column. For each vector index, we need to create a separate Vector
LSM, configured with the appropriate distance function and other parameters, on
each tablet replica.

### Management of vector ids during deletes and overwrites

The indexed table is organized using the primary key, and the vector index
stores pairs of (vector_id, vector). Vector search returns a list of vector ids
and distances. We need to be able to map those vector ids back to the primary
keys of the indexed table, so that we can retrieve other useful data from it.

We chose to store this additional mapping in RockDB, next to the main table's
data. The DocDB encoding we use for these special DocDB keys ensures that they
occupy a separate section of the RocksDB key space. From DocDB's point of view,
these special keys should be encoded in an MVCC-comptabile way, just like all
other keys, with a hybrid timestamp at the end of each encoded key. Similarly,
vector ids can be deleted by setting the RocksDB value to a tombstone value
type. The "ybctid" below identifies the primary key of the indexed table. The
name "ybctid" comes from a combination of the "yb" prefix for YugabyteDB and
the "CTID" concept from Postgres, but it simply stores the primary key of the
main table.

```
vector_id1, timestamp2 -> <DEL>
vector_id1, timestamp1 -> ybctid1
vector_id2, timestamp3 -> ybctid2
```

We require that vector_ids are generated uniquely for each vector insert or
update operation. This is achieved by using UUIDs for vector_ids, but other
solutions are theoretically possible, such as using uint64 numbers assigned
by a mechanism similar to PostgreSQL sequences.

In addition to the above "reverse mapping" of vector ids back to primary keys
for the indexed table, we also need to store the vector ids next to the vector
itself. The DocDB encoding from an earlier example thus can be rewritten more
precisely in the following way:

```
id1, document_content_column_id, t10 -> content1
id1, embedding_column_id, t10 -> vector1, vector_id1
id2, t40 -> <DEL>
id2, document_content_column_id, t20 -> content2
id2, embedding_column_id, t30 -> vector3, vector_id3
id2, embedding_column_id, t20 -> vector2, vector_id2
```

This two-way mapping between vectors and vector ids has to be maintained even
before any vector indexes are added to the system. It is achieved by
proactively assigning vector ids and maintaining the mapping for every
vector-typed column. If multiple vector indexes are added on the same column,
the same vector id can be used to represent a particular vector in each of
those indexes.

Note that even if an identical vector is repeatedly inserted into the same row
or a different row of the table, it is always assigned a new unique vector id.
Thus, the vector id does not identify a vector, but the event of inserting a
vector into the table or updating a vector column in an existing row.

To support cleanup of the reverse mapping of vector id to ybctid in the event
of a vector column deletion, it would be advantageous to also store the column
id either as part of the key, or as part of the value, in the reverse mapping's
RocksDB key/value pairs. This way, stale data could be cleaned up during
compactions similarly to how cleanup of normal RocksDB data for deleted columns
works.

### Changes to the write path for maintaining vector id mapping

The write path changes in this section are in the logic that happens prior to
Raft replication.

Inserting or updating a vector-typed column involves the following:
- Generating a new unique vector id (random UUID) for the new vector.
- Adding the vector_id to the direct mapping, encoded in the DocDB value of the
vector.
- Adding the reverse vector_id -> ybctid mapping.
- Adding deletion of the old vector_id -> ybctid mapping. This requires reading
the old value of the vector_id first.

The above changes should be done transactionally as part of the same write
batch that inserts or updates the vector. For single-shard operations, the
vector id mapping key/value pairs would be added to the same Raft-replicated
write batch with other changes, and for cross-shard transactional operations,
these key/value pairs would be added to the intent batch. Intents/locks should
still be acquired on the row/column as usual. It might not be necessary to
acquire intents/locks on the vector id, but careful analysis of a situation
when multiple transactions are trying to update the vector in the same row is
needed. The initial implementation should probably acquire locks/intents on the
vector id key of the reverse mapping record as if it were a normal DocDB key.

### Changes to the write path for writing to Vector LSM

It makes sense to write the vector to the Vector LSM immediately after writing
it to regular RocksDB. This way, the vectors present in the vector LSM will
logically match those present in regular RocksDB. This write happens after Raft
replication. DocDB writes to regular RocksDB primarily in two cases:

- After successful Raft replication of a non-transactional operation.

- While applying intents for a transaction, which happens after successful
Raft replication of a transaction APPLY operation.

In these cases, we have to implement additional logic to decode the vector from
the write batch, extract the vector id, and insert the (vector_id, vector) pair
into the vector LSM. Again, this is all happening _after_ Raft replication. The
vector id would have been added to the write batch next to the vector itself
_before_ Raft replication.

Note that when we say that "Vector LSM data logically matches vectors stored in
regular RocksDB", it does not mean this is an invariant that is maintained at
all times. Regular RocksDB and Vector LSM have independent background flushes,
and after a tablet server restart, the on-disk state of the two persistent
systems will most likely be different. However, the tablet bootstrap (recovery)
procedure will bring these two systems in sync. Read the recovery section
below for more details.

### Flush order of Intents RocksDB and Vector LSM

We already have a dependency between the order of flushing intents RocksDB and
regular RocksDB to avoid data loss if intents are applied to regular DB and
deleted from intents DB, with the changes flushed in the intents DB (with the
intents lost) and not flushed into regular DB, prior to a tserver crash. The
diagram below illustrates a similar situation that might happen with the
relative order of flushing intents DB and Vector LSM, in a transactional
workload modifying a table with a vector index.

* Write some data on behalf of transaction txn1

* Commit transaction txn1

* The transaction coordinator sends a transaction APPLY message to the
participating tablet and it gets replicated in Raft

* As part of processing this APPLY message, we read the intents from the
intents RocksDB, write the corresponding key/value pairs to regular RocksDB,
and delete those intents from the intents RocksDB.

* At some point later, we flush the regular RocksDB, then flush the intents D
RocksDB, and then the tablet server crashes.

<img
  src="https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/architecture/images/vector-index-flush-vector-lsm-before-intents.svg"
  align="center"
  alt="Flush vector LSM before flushing Intents DB to avoid data loss" />

In this case, the data written to the vector LSM on behalf of txn1 has been
lost. To remedy this, we need to introduce a similar dependency between the
flush order of intents RocksDB and vector LSM that exists between the flush
order of intents RocksDB and regular RocksDB.

### Individual buffers and vector indexes in Vector LSM

Vector LSM is organized similarly to other LSM-based systems such as RocksDB,
although with some crucial differences. At any given point, it contains an
in-memory buffer for recently added entries, a _mutable chunk_ and a number of
_immutable chunks_, each chunk represented by a separate vector index managed
by the underlying vector indexing library. In the steady state, immutable
chunks are stored on disk and paged into memory as needed using a mechanism
such as memory mapped files. The mutable chunk is an in-memory index currently
under construction. This in-memory index is configured with a particular fixed
capacity.

The workflow is as follows:

- New vectors are added to an in-memory buffer.

- In the background, in a multi-threaded way, the data from the in-memory
buffer are added to the current mutable chunk.

- Once the current mutable chunk fills up, we make it immutable and flush it to
disk. We then reopen it as a file-based vector index, and add the file name to
a metadata file similar to RocksDB manifest to make it live.

- If the server crashes, the in-memory buffer and the contents of the immutable
chunks are lost, but we have a mechanism to re-add that data to the vector index
described in the next section.

- During server restart, we read the live chunks from the manifest file and add
them to our list of immutable chunks.

### Recovery mechanism after tablet server restart

Similarly to the "frontier" mechanism in DocDB, which keeps track of the
maximum Raft OpId persistently stored in SSTable files, we maintain frontier
values in the Vector LSM manifest/metadata file. During recovery (tablet
bootstrap), we look at the metadata for all Vector LSMs of the tablet and
replay the missing write operations against those LSMs.

Note that the frontiers for the tablet's regular and intent RocksDB, and the
frontiers for the tablet's Vector LSM might all be different, resulting in
a different set of operations replayed against each persistent storage system.

A general note on Raft's concept of persistent state machine: in YugabyteDB,
and, most likely, in other Raft-based databases, Raft WAL records are not
applied immediately to the underlying "persistent state machine", such as
RocksDB or Vector LSM, unlike how it is described in the Raft paper. For
performance reasons, they are applied to in-memory state, which might get lost
during a server crash. However, with each operation, we keep track of the OpId
of the Raft WAL record containing this operation, and apply that knowledge to
the in-memory state of the persistent system. When this in-memory state is
flushed to disk, the OpId "frontier" mechanism records it within the persistent
system's metadata, ensures that we always know what records to replay during
recovery (bootstrap).

### Vector LSM compactions

With a sustained write workload, the vector LSM will keep generating new chunks
(vector index files) of fixed size for new data. The maximum number of vectors
per vector index chunk is a configurable parameter, conceptually similar to
memtable size in RocksDB. As the number of these files grows, in order to
maintain acceptable read performance, it becomes necessary to automatically
combine these vector index chunk files into new files in the background. While
doing so, we can also omit permanently deleted data from the newly generated
files. This procedure is similar to RocksDB compactions. Below are some
approaches we could use, inspired by RocksDB:

- A size amplification based system similar to RocksDB's universal compactions,
where we keep adding files to be compacted, starting from the newest to the
oldest file, continuing while ratio of the next considered file size to the
total size of all preceding files is within a certain range.

- Minimum and maximum number of files to compact.

- Minimum total file size to compact.

In addition to this, we should consider the percentage of permanently deleted
vectors in a vector index file. By "permantently deleted" we mean deleted at a
timestamp ht <= history_cutoff. Until those records are cleaned up, they will
be encountered during handling of read queries, and will have to be filtered
out either by a predicate pushed down to the vector search library, or by
a post-filtering step. In both of these cases, query performance might be
significantly impacted. Therefore, we should have a threshold for the
percentage of permanently deleted vectors after which we consider a chunk file
as an input for a compaction, even if this "compaction" becomes a special
single-file filtering procedure.

We should also keep in mind that merging vector index chunks is much more
computationally complex than merging SSTable files. Also, in the absence of
a specialized merging algorithm for the particular type of vector index
that is optimized for reduced memory footprint, we need to allocate the amount
of memory proportional to the maximum size of the compaction's output. This
might impose a limitation on the total size of a single vector index chunk
that we might hope to produce as part of compactions, and increase the memory
requirements for the cloud instance types used for tablet server nodes.

### Filtering of vector search results to satisfy MVCC constraints

To satisfy transactional guarantees during the vector index read path, we need
to consider the read timestamp of the read request. We should filter out
vectors in the following two categories:

- Vectors that have been overwritten or deleted at a timestamp higher than
read_time. (Both overwrites and deletes are represented with a delete marker
against the vector_id in the regular RocksDB.)

- Vectors that were inserted at timestamp higher than read_time.

The following diagram illustrates this filtering process. Suppose the nearest M
vectors for the given query found in the first index chunk correspond to
identifiers vector_id3, vector_id6, vector_id7, and vector_id9. The number M of
nearest vectors that we must attempt to retrieve from each chunk might be
higher than the user-specified number of nearest neighbors K for the top-level
query, as we will see shortly. When looking up the vector_id mapping in regular
RocksDB, we find that vector_id3 has already been deleted as of read_time
(which is equal to ht8 in this example), and vector_id9 is too new considering
the read time. Therefore, we end up only with the two vectors identified by
vector_id6 and vector_id7 from this chunk.

<img
  src="https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/architecture/images/vector-index-mvcc-post-filtering.svg"
  align="center"
  alt="MVCC post-filtering of vector search results" />

In this post-filtering approach, we might end up with fewer than the
user-specified number K of results from a particular index chunk. In that case,
we have to repeat the search in that particular chunk with an increasing number
M of requested results (e.g. doubling that value at every retry) until the
filtered list contains at least K entries. Finally, we merge the results from
all chunks, memory buffers and intents into the final result list of K entries
where every entry satisfies MVCC visibility rules.

To reduce or eliminate the retries caused by insufficient number of matching
results, we should consider filtering the vectors inside the search algorithm
itself. Both Usearch and Hnswlib support predicate pushdown into the search
implementation. The insertion hybrid time of a vector is known at vector
insertion time, so it makes sense to store it in the index, perhaps as part of
the label, and filter out vectors that are too new in the predicate callback.
The deletion checking, on the other hand, would involve a RocksDB read of the
record corresponding to the appropriate vector_id, so we need to be careful to
avoid slowing down HNSW graph search with this heavyweight operation. Any
callback that involves reading from RocksDB / DocDB needs to be designed
carefully, e.g. optimizing for the case when there are no deletions, using
Bloom filters on the vector id, and checking for deletions on any given vector
id no more than once.

## Conclusion

In summary, the vector indexing architecture in YugabyteDB adds practical and
efficient vector search capabilities to the distributed SQL database. By
combining the familiar pgvector SQL interface with proven vector indexing
libraries like Usearch and Hnswlib, this architecture supports high-performance
vector data management on a large scale, fitting use cases like recommendation
systems and document search.

The Vector LSM subsystem enables YugabyteDB to handle vector data efficiently
by using in-memory and on-disk indexes with periodic compaction, balancing
speed and storage use. Copartitioning with tables improves read performance by
keeping vector indexes and table data close together.

Overall, this design allows YugabyteDB to support vector-based applications
with minimal friction for users, offering an accessible way to work with both
SQL and vector data in a single, distributed database system. Future
enhancements could add more indexing algorithms and optimizations, making
YugabyteDB a flexible choice for applications that blend relational and vector
data.

## References

Malkov, Yu. A., & Yashunin, D. A. (2018). *Efficient and robust approximate nearest neighbor search using Hierarchical Navigable Small World graphs*. https://arxiv.org/abs/1603.09320

Ongaro, D., & Ousterhout, J. (2014). *In Search of an Understandable Consensus Algorithm*. USENIX Annual Technical Conference. [https://raft.github.io/raft.pdf](https://raft.github.io/raft.pdf)

*Universal compactions in RocksDB.* https://github.com/facebook/rocksdb/wiki/Universal-Compaction

*Usearch vector indexing library.* https://github.com/unum-cloud/usearch

*Hnswlib vector indexing library.* https://github.com/nmslib/hnswlib

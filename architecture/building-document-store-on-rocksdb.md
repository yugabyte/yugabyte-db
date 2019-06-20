<h1>Building a high performance document store on RocksDB</h1>

<a href="https://rocksdb.org/">RocksDB</a> is a popular embeddable persistent key-value store. 
First open sourced by Facebook in 2012 as a fork of the Google LevelDB project, it has been 
adapted over the years to a wide range of workloads including database storage engines and 
application data caching.

In this document, we explain our rationale for selecting RocksDB as a foundational building
block  for YugaByte DB. We also describe how we model rows in YugaByte DB as documents that 
then  get stored as multiple  key-value pairs in RocksDB.

<h2 style="text-align: left;">Why Does a Database Need Another Database?</h2>

One might wonder why a database like <a href="https://github.com/YugaByte/yugabyte-db">YugaByte DB</a> 
depends on another database like RocksDB. While both have the letters “DB” in their name, they serve 
significantly different needs.

RocksDB is a <strong>monolithic key-value storage engine</strong> with somewhat lower-level APIs. 
For example, key/values are just byte arrays and not typed. It’s designed for fast access, persistence 
and embedding in the context of a single node. Given the single node focus, it does not consider 
high availability and geographic data distribution as design goals.

On the other hand, YugaByte DB is a <strong>distributed SQL database</strong> for building internet-scale, 
globally-distributed applications with features such as multi-shard (aka distributed) transactions that 
access keys on different nodes. These nodes can be in a single datacenter/region or can be far apart in 
different regions (with unpredictable WAN latencies between the regions). YugaByte DB supports YSQL, 
a fully relational SQL API for scale-out RDBMS workloads, as well as YCQL, a SQL-based flexible schema 
API for internet-scale workloads.

A distributed SQL database is a complex integration of software comprised of several components:
<ul>
 	<li>API layer responsible for language-specific query compilation, execution &amp; optimization</li>
 	<li>Replication &amp; transaction coordination</li>
 	<li>Data sharding &amp; load balancing</li>
 	<li>Failure detection &amp; recovery components</li>
 	<li>Last but not least, a per-node (or node-local) storage engine</li>
</ul>

Specifically, YugaByte DB is comprised of two high level layers - 
<a href="https://docs.yugabyte.com/latest/architecture/concepts/yql/">YugaByte Query Layer (YQL)</a> 
is the API layer and <a href="https://docs.yugabyte.com/latest/architecture/concepts/docdb/">DocDB</a> is 
the distributed document store.

<img class="alignnone wp-image-634 aligncenter" 
src="https://blog.yugabyte.com/wp-content/uploads/2019/02/yugabyte-db-rocksdb-docdb1.png" 
alt="" width="404" height="388" />
&nbsp;

YQL implements the APIs supported by YugaByte DB and runs on DocDB, the datastore common across all YQL APIs.

Irrespective of whether a database is distributed or monolithic, a well-designed per-node storage 
engine is the critical to reliability, efficiency and performance of the database. This is the precise 
reason why DocDB uses <strong>a highly customized version of RocksDB as its per-node storage engine.
</strong> As described in <a href="extending-rocksdb.md">“Extending RocksDB for 
Speed &amp; Scale”</a>, this engine is fully optimized for high performance and large datasets. All 
changes made to RocksDB are distributed as part of <a href="https://github.com/YugaByte/yugabyte-db">
this open source Apache 2.0 project</a>.

<h2 style="text-align: left;">Why RocksDB?</h2>

We were data infrastructure engineers at Facebook during the years when RocksDB was under initial 
development. Watching RocksDB grow right in front of us into an ideal datastore for low latency 
and high throughput storage (such as flash SSDs) was exciting to say the least.

When we started the YugaByte DB project in early 2016, RocksDB was already a mature project thanks 
to its wide adoption across Facebook and other web-scale companies including Yahoo and LinkedIn. 
This gave us the confidence to use it as a starting point for YugaByte DB.

The other two other key factors in our decision to use RocksDB were its Log Structured Merge 
tree (LSM) design and our desire to implement YugaByte DB in C++.

<h3 style="text-align: left;">LSM vs. B-Tree: Is There a Clear Winner?</h3>

LSM storage engines like RocksDB have become the de facto standard today for handling workloads 
with fast-growing data especially as SSDs keep getting cheaper.

Google’s OLTP databases, namely Spanner and its precursor BigTable, use an LSM store. Facebook’s 
user databases <a href="https://code.fb.com/core-data/myrocks-a-space-and-write-optimized-mysql-database/">
migrated away</a> from using MySQL with an InnoDB (B-Tree) engine to MySQL with 
RocksDB-based MyRocks (LSM) engine. MongoDB’s WiredTiger storage engine offers an LSM option 
as well even though MongoDB itself does not support that option. Newer time series data stores such 
as InfluxDB also follow the LSM design.

As previously described in <a href="https://blog.yugabyte.com/a-busy-developers-guide-to-database-storage-engines-the-basics/">“A Busy Developer’s Guide to Database Storage Engines”</a>, there are two primary 
reasons behind LSM’s overwhelming success over B-Trees.

First reason is that writes in an LSM engine are sequential, and SSDs handle sequential writes 
much better than random writes. As explained in this <a href="https://www.seagate.com/tech-insights/lies-damn-lies-and-ssd-benchmark-master-ti/">Seagate article</a>, sequential writes dramatically 
simplifies garbage collection at the SSD layer while random writes (as in a B-Tree engine) 
causes SSD fragmentation. The defragmentation process impacts SSD performance significantly.

Secondly, even though the conventional wisdom is that B-Trees are better for read intensive-workloads,
the reality is that this wisdom is inaccurate for many modern workloads.
<ul>
 	<li>LSMs require more read IOPS from the drives in general. However, unlike HDDs, SSDs 
		offer very high read throughput at microsecond latencies for this to be a major 
		issue with modern hardware.</li>
 	<li>For random read workloads, bloom filters help reduce the number of read IOPS often 
		making it close to B-Tree style workloads by trading off some CPU.</li>
 	<li>For read-recent workloads (e.g., “Get top 50 messages of an inbox or recent 6 
		hours of activity for a device”), LSMs outperform B-Trees. This is due to the fact 
		that in an LSM data is naturally time partitioned, while still being clustered by 
		say the “user id” (in a Inbox-like application) or “device id” (as in a IOT or 
		time series application).</li>
</ul>

<h3 style="text-align: left;">Language Matters: C++, Java, or Go?</h3>

RocksDB is written in C++ with the intent of exploiting the full throughput and low-latency 
of fast SSDs &amp; non-volatile memory (NVM). For similar reasons, YugaByte DB is implemented in C++.

We didn’t want to put an inter-language switch in the critical path of a database engine’s 
IO. For example, CockroachDB, another database that uses RocksDB as its storage engine, 
incurs a Go &lt;=&gt; C++ switch in the critical IO path because the query execution layer is 
in Go language and storage engine (RocksDB) is C++. The number of these language-boundary 
hops needed to process fairly simple queries can still be significant and this is one of 
the factors that adversely
<a href="https://blog.yugabyte.com/yugabyte-db-vs-cockroachdb-performance-benchmarks-for-internet-scale-transactional-workloads/">impacts performance.</a>

We resisted the temptation to develop a high performance database in a garbage collected 
language such as Java (e.g., Apache Cassandra) or Go (e.g., CockroachDB). Such implementations 
are unable to push fast storage to its full potential or to leverage large amounts of RAM.

By virtue of YugaByte DB and RocksDB both being in C++, we are able to achieve an in-depth &amp; 
low-overhead integration of RocksDB with the rest of the system. Such an integration allows us 
to pass data as efficiently as possible (often by reference) between the various components 
of the system.

<h2 style="text-align: left;">Introducing DocDB</h2>
As noted previously, <a href="https://docs.yugabyte.com/latest/architecture/concepts/docdb/">DocDB</a> 
is YugaByte DB’s distributed document store. Irrespective of the API used, every row managed by 
YugaByte DB is internally stored in DocDB as a separate document. This enables YugaByte DB to have a
pluggable API architecture where new APIs can be added on top of the common document data model. 
Each DocDB document is mapped to multiple key-value pairs in RocksDB, the underlying per-node 
storage engine.


<h3>Data Types</h3>
DocDB <a href="https://github.com/YugaByte/yugabyte-db/commit/006d0dccf132c02b11c66d5eeb7da225df2e44b4">
extends RocksDB</a> to efficiently handle documents that include both primitive and non-primitive 
types (such as maps, collections, and JSON). The values in DocDB can be:
<ul>
 	<li><strong>Primitive types:</strong> such as int32, int64, double, text, timestamp, etc.</li>
 	<li><strong>Non-primitive types (stored as sorted maps):</strong> Objects that map 
  scalar subkeys to values, which could themselves be either scalar or nested object types.</li>
</ul>

<h3>Mapping Documents to RocksDB Key-Values</h3>

The DocDB <a href="https://docs.yugabyte.com/latest/architecture/concepts/persistence/#encoding-details">data 
model</a> allows multiple levels of nesting, and corresponds to a JSON-like format. Other data structures 
like lists, maps and sorted sets are implemented using DocDB’s object type with special key encodings.

For example, consider the following document stored in DocDB.
<pre class="lang:default decode:true">user-123 = {
	addr = {
		city = San Francisco
		zip = 94103
	},
	age = 36
}</pre>

The above document is stored in RocksDB as a set of keys. Each key stored in 
RocksDB consists of a number of components, where the first component is a 
“document key”, followed by a few scalar components, and finally followed by 
a MVCC timestamp (sorted in reverse order). When we encode primitive values 
in keys, we use a binary-comparable encoding, so that the sort order in 
RocksDB ends up to be the desired one.

Assume that the sample document above was written at time T10 entirely.  Internally this 
is stored using five RocksDB key-value pairs:

<pre class="lang:default decode:true">user-123, T10 -&gt; {} // This is an init marker
user-123, addr, T10 -&gt; {} // This is a sub-document init marker
user-123, addr, city, T10 -&gt; San Francisco
user-123, addr, zip, T10 -&gt; 94103
user-123, age, T10 -&gt; 36</pre>

DocDB uses RocksDB as an append-only store so that deletions of documents or nested documents 
are performed by writing a single tombstone marker at the corresponding level. Customized 
read hooks automatically recognize these special markers and suppress expired data. Expired
values within the sub-document are cleaned up/garbage collected by our customized compaction 
hooks.

<h3>Serving Workloads with Low Latency &amp; High Throughput</h3>

DocDB ensures that the database read/write operations can be executed at the 
lowest cost possible by using multiple techniques. Following are a few examples.

<ul>
 	<li>Fine-grained updates to only a small subset of a row (such as a column 
  in the YSQL API) or an element in a collection (such as in the YCQL API) without 
  incurring a read-modify-write penalty of the entire row or collection.</li>
 	<li>Delete/overwrite of a row or collection/object at an arbitrary nesting 
  level without incurring a read penalty to determine the specific set of KVs 
  that need to be deleted</li>
 	<li>Efficient row/object-level TTL handling by tightly hooking into the 
  “read/compaction” layers of the underlying RocksDB KV store.</li>
</ul>

<h2 style="text-align: left;">Summary</h2>
RocksDB is arguably one of the most successful open source data infrastructure projects in the last
decade. It is second to none when it comes to fast monolithic key-value storage. YugaByte DB leverages 
RocksDB’s strengths in the context of embedding it as the per-node storage engine of DocDB, YugaByte DB's distributed 
document store. Every row managed by YugaByte DB is stored as a document in DocDB that internally 
maps to multiple key-value pairs in RocksDB. As described in
<a href="extending-rocksdb.md">“Extending RocksDB for Speed &amp; Scale”</a>, 
we also had to make significant enhancements to RocksDB in this context. 


Building a massively scalable, high performance SQL database is a hard engineering problem to say the least. 
Using RocksDB as a foundational building block certainly made life easier for us as an engineering team. 
Needless to say, we are thankful to the entire RocksDB community for their phenomenal work over the years.

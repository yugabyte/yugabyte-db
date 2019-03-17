<h1> Extending RocksDB for Speed & Scale </h1>

As described in <a href="building-document-store-on-rocksdb.md">
“Building a High Performance Document Store on RocksDB”</a>, YugaByte DB’s distributed 
document store (DocDB) uses RocksDB as its per-node storage engine. We made multiple 
performance and high data density related enhancements to RocksDB in the course of embedding 
it into <a href="https://docs.yugabyte.com/latest/architecture/concepts/docdb/persistence/">DocDB’s 
document storage layer</a> (figure below).</a> 

RocksDB’s immense  popularity as a fast embeddable storage engine combined with its 
Log-Structured Merge trees (LSM) design and C++ implementation were the critical 
factors in selecting it  as DocDB’s per-node storage engine. Every row managed by 
YugaByte DB is stored as  a document in DocDB that internally maps to multiple 
key-value pairs in RocksDB.

As we started building the document storage layer in DocDB, we realized that we need 
to enhance RocksDB significantly. This is because each RocksDB instance could no 
longer operate in isolation. It needed to share resources with other RocksDB instances 
present on the same node. Additionally, we also wanted to support very large data sets
per node, while keeping memory requirements reasonable.

In this document, we take a close look at these enhancements to RocksDB.

<p style="text-align: center;">
<img class="aligncenter wp-image-644 size-full" 
src="https://blog.yugabyte.com/wp-content/uploads/2019/02/docdb-rocksdb.png" 
alt="" width="787" height="445" />
</p>


<h2 style="text-align: left;">Block-based Splitting of Bloom/Index Data</h2>
RocksDB’s SSTable files contain data and metadata such as indexes &amp; bloom 
filters. The data portion of an SSTable file was already chunked into blocks 
(32KB default) and demand-paged.

However, the bloom filter &amp; index portions were monolithic (at least when we 
started using RocksDB in 2016) and needed to be brought into memory in an 
all-or-nothing manner. For large datasets, this increases memory requirements and causes 
memory fragmentation.

We <a href="https://github.com/YugaByte/yugabyte-db/commit/147312863b104d2d4b2f267cbb6b4fc95f35f3a8">
enhanced RocksDB’s index and bloom filters</a> to be multi-level/block-oriented structures 
so that these metadata blocks can be demand-paged in uniform sized chunks into the block cache 
much like data blocks.  This enables YugaByte DB to support very large data sets in a RAM efficient 
and memory allocator friendly manner.

<h2 style="text-align: left;">Multiple Instances of RocksDB Per Node</h2>

DocDB auto shards tables into multiple tablets. It dedicates one RocksDB 
instance per tablet as opposed to sharing a single RocksDB instance across 
multiple tablets on a node.

<h3 style="text-align: left;">Benefits of the Design</h3>
<ul>
 	<li>Cluster rebalancing on node failure or node addition becomes extremely 
  efficient because the SSTable files of tablets being rebalanced 
  <a href="https://github.com/YugaByte/yugabyte-db/commit/3ff3aba80e0fdcf006996b85e93093b816a3241f">
  can be copied as is (in their compressed form) from tablet leaders.</a> Unlike a scheme 
  where multiple tablets share a single RocksDB instance, no logical scan or splitting 
  of SSTable files is required. And no need to wait for compactions to reclaim storage 
  from the nodes a shard previously lived on!</li>
 	<li>Deleting a table is as simple as dropping the related RocksDB instances.</li>
 	<li>Allows for per-table storage policy
<ul>
 	<li>On-disk compression - on/off? which compression algorithm? etc.</li>
 	<li>In-memory delta-encoding scheme - e.g., prefix-compression or diff encoding</li>
</ul>
</li>
 	<li>Allows for per-table bloom-filter policy. For tables with compound primary keys 
  (say <code>(&lt;userid&gt;, &lt;message-id&gt;)</code>), which portion(s) of the key
  are added to the bloom-filter depends on the access pattern to optimize for. Adding 
  the entire compound key to the bloom is useless, and pure overhead, if application 
  queries are commonly going to provide only one portion of the compound key (say 
  just the <code>&lt;user-id&gt;</code>).</li>
 	<li>Allows DocDB to track min/max values for each clustered column in the primary 
  key of a table (<a href="https://github.com/YugaByte/yugabyte-db/commit/14c7da8008d2a8a691cf5e1a858167ccfe773b9d">
  another enhancement to RocksDB</a>) and store that in the corresponding SSTable file as metadata. 
  This enables YugaByte DB to optimize range predicate queries like by minimizing the number 
  of SSTable files that need to be looked up:</li>
</ul>
<pre class="lang:default decode:true">SELECT metric_val 
  FROM metrics
 WHERE device=? AND ts &lt; ? AND ts &gt; ?</pre>
RocksDB already allows for running multiple instances within a single process. 
However, in practice, using RocksDB in this form has required some careful 
engineering &amp; enhancements. We have listed some of the important ones here.

<h3>Server-global Block Cache</h3>

DocDB uses a <a href="https://github.com/YugaByte/yugabyte-db/commit/3d8e83e4298bcf4cca2fd84a58e2f237c925ba30">
shared block cache across all instances</a> of RocksDB on the server. This avoids per-tablet 
cache silos and increases effective utilization of memory.

<h3>Server-global Memstore Limit</h3>

RocksDB allows a per-memstore flush size to be configured. This is not sufficient in 
practice because the number of memstores may change over time as users create new tables, 
or tablets of a table move between servers due to load balancing. Picking a very small 
value for the per-memstore flush size results in premature flushing and increases 
write amplification. On the other hand picking a very large per-memstore flush size,
for a node with lots of tablets, increases memory requirement on the system and also 
the recovery time (in case of server restart).

To avoid these issues, we <a href="https://github.com/YugaByte/yugabyte-db/commit/faed8f0cd55e25f2e72c39fffa72c27c5f84fca3">enhanced RocksDB to enforce a 
 global memstore limit.</a> When the memory used by all memstores reaches this limit, 
 the memstore with the oldest record (determined using hybrid timestamps) is flushed.
 
<h3>Separate Queues for Large &amp; Small Compactions</h3>

RocksDB supports a single compaction queue with multiple compaction threads. But 
this leads to scenarios where some large compactions (by way of the amount of data 
to read/write) get scheduled on all these threads and end up starving the smaller 
compactions. This leads to too many store files, write stalls, and high read latencies.

We <a href="https://github.com/YugaByte/yugabyte-db/commit/dde2ecd5ddf4b01879e32f033e0a80e37e18341a">
enhanced RocksDB to enable multiple queues</a> based on the total input data files size in order 
to prioritize  compactions based on the sizes. The queues are serviced by a configurable number 
of threads, where a certain subset of these threads are reserved for small compactions so that 
the number of SSTable files doesn’t grow too quickly for any tablet.

<h3>Smart Load Balancing Across Multiple Disks</h3>

DocDB supports a just-a-bunch-of-disks (JBOD) setup of multiple SSDs and doesn’t require a 
hardware or software RAID. The RocksDB instances for various tablets are 
<a href="https://github.com/YugaByte/yugabyte-db/commit/d53de140eccaf7bfd31b938a4a8d5bd88d950329">balanced 
across the available SSDs uniformly</a>, on a per-table basis to ensure that each SSD has a 
similar number of tablets from each table and is taking uniform type of load. Other types 
of load balancing in DocDB are also done on a <strong>per-table basis</strong>, be it:

<ul>
 	<li>Balancing of tablet replicas across nodes</li>
 	<li>Balancing of leader/followers of Raft groups across nodes</li>
 	<li>Balancing of Raft logs across SSDs on a node</li>
</ul>


<h2 style="text-align: left;">Scan Resistant Cache</h2>

We <a href="https://github.com/YugaByte/yugabyte-db/commit/0c6a3f018ac90724ac1106ff248c051afbdd6979">enhanced 
RocksDB’s block cache</a> to be scan resistant. This prevents operations such as long-running 
scans (e.g., due to an occasional large query or a background Spark job) from polluting the 
entire cache with poor quality data and wiping out useful/hot data. The new block cache uses 
a <a href="https://dev.mysql.com/doc/refman/8.0/en/midpoint-insertion.html">MySQL</a>/
<a href="https://github.com/apache/hbase/blob/master/hbase-server/src/main/java/org/apache/hadoop/hbase/io/hfile/LruBlockCache.java">
HBase</a> like mid-point insertion strategy where the LRU is divided into two portions 
and multiple touches to a block are required before it is promoted to the 
multi-touch/hot portion of the cache.

<h2 style="text-align: left;">Additional Optimizations</h2>

As noted in our <a href="building-document-store-on-rocksdb.md">previous post</a>, DocDB manages 
transactional processing, replication, concurrency control, data time-to-live (TTL), and 
recovery/failover mechanisms at the overall cluster level as opposed to the per-node 
storage engine level. Therefore, some of the equivalent functionality provided by 
RocksDB became unnecessary and were removed.

<h3>Double Journaling Avoidance in the Write Ahead Log (WAL)</h3>

DocDB uses the Raft consensus protocol for replication. Changes to the distributed system, 
such as row updates, are already being recorded and journaled as part of the Raft logs. 
The additional WAL mechanism in RocksDB is unnecessary and would only add overhead.

DocDB avoids this double journal tax by disabling the RocksDB WAL, and instead relies 
on Raft log as the source of truth. It tracks the Raft “sequence id” up to which data 
has been flushed from RocksDB memtables to SSTable files. This ensures that we can 
correctly garbage collect Raft logs, as well as, replay the minimal number of records
from Raft WAL logs on a server crash or restart.

<h3>Multi-Version Concurrency Control (MVCC)</h3>

DocDB manages MVCC on top of RocksDB at the overall cluster level. The mutations to 
records in the system are versioned using hybrid timestamps. As a result, the notion
of MVCC as implemented in a vanilla RocksDB using sequence ids adds overhead. For this
reason, DocDB does not use RocksDB’s sequence ids, and instead uses hybrid timestamps
that are part of the encoded key to implement MVCC.

<blockquote>Hybrid Logical Clock (HLC), the hybrid timestamp assignment algorithm, 
is a way to assign timestamps in a distributed system such that every pair of "causally 
connected" events results in an increase of the timestamp value. Please refer to these 
reports (<a href="http://users.ece.utexas.edu/~garg/pdslab/david/hybrid-time-tech-report-01.pdf">#1</a> 
or <a href="https://cse.buffalo.edu/tech-reports/2014-04.pdf">#2</a>) for more details.
</blockquote>



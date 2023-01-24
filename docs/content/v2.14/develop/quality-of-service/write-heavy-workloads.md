---
title: Write-heavy workloads
headerTitle: Write-heavy workloads
linkTitle: Write-heavy workloads
description: Write-heavy workloads in YugabyteDB.
headcontent: Write-heavy workloads in YugabyteDB.
image: <div class="icon"><i class="fa-solid fa-file-invoice-dollar"></i></div>
menu:
  v2.14:
    name: Write-heavy workloads
    identifier: develop-quality-of-service-write-heavy-workloads
    parent: develop-quality-of-service
    weight: 230
type: docs
---

YugabyteDB has extensive controls in place to slow down writes when flushes or compactions cannot keep up with the incoming write rate. Without this, if users keep writing more than the hardware can handle, the database will:

* Increase space amplification, which could lead to running out of disk space
* Increase read amplification, significantly degrading read performance

The idea is to slow down incoming writes to the speed that the database can handle. In these scenarios, the YugabyteDB slows down the incoming writes gracefully by rejecting some or all of the write requests.

## Identifying reasons for write stalls

Write stalls can occur for the following reasons:

* Low CPU environments
* Disks with low performance

The following symptoms occur at the database layer:

### Flushes cannot keep up

More writes than the system can handle could result in too many memtables getting created, which are queued up to get flushed. This puts the system in a suboptimal state, because a failure would need a large rebuild of data from WAL files, as well as requiring too many compactions for the system to catch up to a healthy state.

### Compaction cannot keep up

The database getting overloaded with writes can also result in compactions not being able to keep up. This causes the SST files to pile up, degrading read performance significantly.

### WAL writes are too slow

If WAL writes are slow, then writes experience higher latency which creates a natural form of admission control. The frequency of data getting synchronized to disk is controlled by the `durable_wal_write flag`. Note that `fsync` is disabled by default, as YugabyteDB is expected to run in a Replication Factor of 3 mode where each tablet is replicated onto three independent fault domains such as hosts, availability zones, regions, or clouds. If enabled, every single write into DocDB, which is a YugabyteDB's underlying document store, will be synchronized to disk before its execution is deemed successful. This means that even though there will be an increase in safety, performance will be negatively affected.

### Limited disk IOPS or bandwidth

In many cloud environments, the disk IOPS and network bandwidth are rate-throttled. Holistically, if there are such disk constraints (limits on IOPS and bandwidth), it results in a back-pressure across all disk writes in the system and would manifest as one of the preceding conditions.

## Write throttling triggers

YugabyteDB defines the following triggers for throttling incoming writes:

### Stop writes trigger

Stop writes trigger is activated in one of the following scenarios:

* **Too many SST files:** The number of SST files exceeds the value determined by the flag `sst_files_hard_limit`, which defaults to 48. After the hard limit is reached, no more writes are processed, all incoming writes are rejected.

* **Memstores flushed too frequently:** This condition occurs if there is a large number of tables that get writes. In such cases, the memstores are forced to flush frequently, resulting in too many SST files. You can tune the total memstore size allocated. Total memstore size is the minimum of the two flags: `global_memstore_size_mb_max` (default value is 2GB) and `global_memstore_size_percentage` (defaults to 10% of total YB-TServer memory allocated). You can use one of the following options to control the amount of memory allocated to YB-TServer:

  * Set `default_memory_limit_to_ram_ratio` to control percentage of total RAM on the instance the process should use.
  * Specify an absolute value using `memory_limit_hard_bytes`. For example, to provide YB-TServer with 32GB of RAM, use `--memory_limit_hard_bytes 34359738368`.

* **Too many memstores queued for flush:** More than one memstore is queued for getting flushed to disk. The number of memstores queued at which this trigger is activated is set to 2 (therefore, 2 or more memstores are queued for flush). Note that in practice, there is always an active memstore, which is not included in this limit.

  {{< warning title="Warning" >}}
  The trigger for too many memstores queued for flush is controlled by the flag `max_write_buffer_number`, which defaults to 2. However, it is not recommended at this time to change this value.
  {{< /warning >}}

### Slowdown writes trigger

Slowdown writes trigger is activated when the number of SST files exceeds the value determined by the flag `sst_files_soft_limit`, but does not exceed the `sst_files_hard_limit` value. The default value of `sst_files_soft_limit` flag is 24. The slowdown in writes is achieved by rejecting a percentage of incoming writes with probability `X`, where `X` is computed as follows:

```
X = (<num SST files> - soft_limit) / (hard_limit - soft_limit)
```

## Admission control trigger and enforcement

The incoming write rejection is computed per tablet. Recall that a tablet consists of tablet peers that participate in Raft consensus, and has a tablet leader elected. The rejection is triggered if one of the conditions described in the previous section occurring on the tablet leader or a majority of the tablet peers. That is, if a single follower lags behind, it is sufficient to let it catch up later. However, if either the leader or a majority of the followers is slow, this might reflect as latency to the request, and therefore warrants some admission control.

The rejection of incoming write requests happen when YugabyteDB gets the write request. If a write request is already being processed by the database (meaning it was accepted initially, and not rejected), then that write will be processed. However, if the write request ends up needing to trigger subsequent writes (for example, certain types of writes that need to update indexes), then those subsequent requests could themselves fail. The rejection is performed at the YB-TServer layer using `ServiceUnavailable` status. The behavior at the query processing layer could just be increased latency and then failure.

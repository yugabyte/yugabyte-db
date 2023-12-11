---
title: Write-heavy workloads
headerTitle: Write-heavy workloads
linkTitle: Write-heavy workloads
description: Write-heavy database workloads in YugabyteDB
headcontent: Write-heavy workloads in YugabyteDB.
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

If WAL writes are slow, then writes experience higher latency which creates a natural form of admission control. The frequency of data getting synchronized to disk is controlled by the `durable_wal_write` flag. Note that `fsync` is disabled by default, as YugabyteDB is expected to run in a Replication Factor 3 mode where each tablet is replicated onto three independent fault domains such as hosts, availability zones, regions, or clouds. Enabling `fsync` means that every write into DocDB (YugabyteDB's underlying document store) must be synchronized to disk before its execution is deemed successful. This increase in safety brings a corresponding performance decrease.

### Limited disk IOPS or bandwidth

In many cloud environments, disk IOPS and network bandwidth are rate-limited. Such disk constraints (limits on IOPS and bandwidth) result in a back-pressure across all disk writes in the system, and manifest as one of the preceding conditions.

## Write throttling triggers

YugabyteDB defines the following triggers for throttling incoming writes:

### Stop writes trigger

Stop writes trigger is activated in one of the following scenarios:

* **Too many SST files**

  The number of SST files exceeds the value determined by the flag `sst_files_hard_limit`, which defaults to 48. Once the hard limit is hit, no more writes are processed, all incoming writes are rejected.

* **Memstores flushed too frequently**

  This condition occurs if there are a large number of tables (or more accurately, a large number of tablets) all of which get writes. In such cases, the memstores are forced to flush frequently, resulting in too many SST files. In such cases, you can tune the total memstore size allocated. Total memstore size is the minimum of the two flags: `global_memstore_size_mb_max` (default value is 2GB) and `global_memstore_size_percentage` (defaults to 10% of total YB-TServer memory allocated. There are 2 different options for controlling how much memory is allocated to YB-TServer:

  * By setting `default_memory_limit_to_ram_ratio` to control what percentage of total RAM on the instance the process should use
  * Specify an absolute value using `memory_limit_hard_bytes`. For example, to give YB-TServer 32GB of RAM, use `--memory_limit_hard_bytes 34359738368`

* **Too many memstores queued for flush**

  More than one memstore is queued for getting flushed to disk. The number of memstores queued at which this trigger is activated is set to 2 (therefore, 2 or more memstores are queued for flush). Note that in practice, there is always an active memstore, which is not included in this limit.

  {{< warning title="Warning" >}}
  The trigger for too many memstores queued for flush is controlled by the `max_write_buffer_number` flag, which defaults to 2. However, it is not recommended at this time to change this value.
  {{< /warning >}}

### Slowdown writes trigger

Slowdown writes trigger is activated when the number of SST files exceeds the value determined by the flag `sst_files_soft_limit`, but does not exceed the `sst_files_hard_limit` value. The default value of `sst_files_soft_limit` flag is 24. The slowdown in writes is achieved by rejecting a percentage of incoming writes with probability `X`, where `X` is computed as follows:

```output
    X = (<num SST files> - soft_limit) / (hard_limit - soft_limit)
```

## Admission control trigger and enforcement

The incoming write rejection is computed at a per *tablet* basis. Recall that a tablet consists of tablet peers that participate in Raft consensus, and has a tablet leader elected. The rejection is triggered if one of the conditions described in the prior section occurring on the tablet leader OR a majority of the tablet peers. The idea here being that, if a single follower lags behind, it is sufficient to just let it catch up later. However, if either the leader or a majority of the followers is slow, this might reflect as latency to the request anyway, and therefore warrants some admission control.

The rejection of incoming write requests happen when YugabyteDB gets the write request. If a write request is already being processed by the database (meaning it was accepted initially, and not rejected), then that write will be processed. However, if the write request ends up needing to trigger subsequent writes (for example, certain types of writes that need to update indexes), then those subsequent requests could themselves fail. The rejection is performed at the YB-TServer layer using `ServiceUnavailable` status. The behavior at the query processing layer could just be increased latency and then failure.

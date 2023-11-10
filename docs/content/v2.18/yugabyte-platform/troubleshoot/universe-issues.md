---
title: Troubleshoot universe issues
headerTitle: Troubleshoot universe issues
linkTitle: Universe issues
description: Troubleshoot universe issues
menu:
  stable_yugabyte-platform:
    identifier: universe-issues
    parent: troubleshoot-yp
    weight: 20
type: docs
---

YugabyteDB Anywhere allows you to monitor and troubleshoot issues that arise from universes.

## Use metrics

A universe's **Metrics** page displays graphs representing information on operations, latency, and other parameters accumulated over time for each type of node and server. Examining specific metrics allows you to diagnose and troubleshoot issues.

You access metrics by navigating to **Universes > Universe-Name > Metrics**, as shown in the following illustration:

![Metrics Page](/images/yp/metrics_main.png)

You can chose to view metrics based on different criteria:

- If your universe has more than one region, cluster, zone, or node, the **Metrics** page allows you to view metrics for a specific region, cluster, zone, or node.

- You can view the overall node metrics which represent an aggregate across the selected nodes. Depending on the metric type, either an average across nodes or a sum of all node values is displayed.

- You can view metrics for up to five top or bottom outlier nodes. For example, to display graphs for the top three nodes with the highest CPU usage, you would select **Outlier Nodes**, then **Top**, and then **3 nodes**. The graph also includes the average for all nodes.

  Note that **Selected Node average** in the graph means the average of all the nodes that you have selected. For example, if you have filtered by region, then the selected node average would be average of all nodes in the selected region.

- You can view metrics for up to seven top or bottom outlier tables, including both system and user tables. For example, to display graphs for two tables with the highest read latency, you would select **Outlier Tables**, then **Top**, and then **2 tables**. The graph also includes the average for all tables.

  Note that the namespace name is appended with the table name and separated by a `.` (dot) operator.

- You can further restrict the outlier nodes or tables graphs to only display metrics for specific operations, events, statuses, and so on. For example, you can find out how many YSQL `SELECT` statements per second have been executed by the top-performing three nodes in the past six hours by selecting **Last 6 hr > Outlier Nodes > Top > 3 nodes > YSQL Ops > Total YSQL Ops / Sec > Select**.

You should do the following on a regular basis:

- Monitor remote bootstrap for node failures. You can access this metric via **Tablet Server > Remote Bootstraps** and **Tablet Server > RPC Queue Size**.
- Monitor the NTP server to ensure that it is running. You can access this information via your computer's operating system.
- Monitor clock skew so you can see when the clock is off by more than 500 milliseconds. You can access this metric via **Node > Clock Skew**.
- Monitor your disk usage. The default is 80% used, but you should consider this metric in the context of your environment. For example, the usage percentage can be higher on larger disks; some file systems issue an alert at 75% usage due to performance degradation. You can access this metric via your computer's operating system.
- Monitor YSQL and YCQL operations latency and tune it according to your application service level agreement. You can access this metric via **YSQL Ops and Latency**, **YCQL Ops and Latency**, **YSQL Advanced**, and **YCQL Advanced**.
- Monitor average read and write latency so you know when latency starts to degrade, at which point you should tune it according to averages. You can access this metric via **Tablet Server > Average Latency**.
- Monitor reactor delays to measure bottleneck of incoming requests. You can access this metric via **Tablet Server > Reactor Delays**.
- Monitor log statistics per node to measure churn in the log cache size to determine high rate of evictions. You can access this metric via **Tablet Server > WAL Stats / Node**.
- Monitor the tablet and master servers' RPC queue size so you can see all the subelements and know if there is a bottleneck that might cause performance degradation. You can access this metric via **Tablet Server > RPC Queue Size** and **Master Server > RPC Queue Size**.
- Monitor DocDB cache miss ratio so you can tune the cache when necessary. You can access this metric via **DocDB > Cache Hit & Miss**.

The following tables describe metrics available via the YugabyteDB Anywhere UI.

### YSQL ops and latency

| **Graph**              | **Description**                                              | **Alert Guidance**                                           | **Example**                       |
| :--------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ | --------------------------------- |
| Total YSQL Ops / Sec   | The count of DELETE, INSERT, SELECT, and UPDATE statements through the YSQL API. <br>This does not include index writes. | An alert should be issued if count drops significantly lower than your average count, as this might indicate an application connection failure. In addition, an alert should be issued if the count is much higher than your average count, as this could indicate a DDoS, security incident, and so on.<br>It is recommended to coordinate this with the application team because there could be legitimate reasons for dips and spikes. | ![img](/images/yp/metrics1.png)   |
| YSQL Op Latency (Avg)  | Average time of DELETE, INSERT, SELECT, and UPDATE statements through the YSQL API. | An alert should be issued when the overall latency is close to or higher than your application SLA.<br>Note that the overall latency metric is less helpful for troubleshooting specific queries. It is recommended that the application track query latency.<br>Also note that there could be reasons your traffic experiences spikes in latency. For example, ad-hoc queries such as count(*) are executed. | ![img](/images/yp/metrics2.png)   |
| Total YSQL Connections | Cumulative number of connections to YSQL backend for all nodes. This includes various  background connections, such as checkpointer, as opposed to an active connections count that only includes the client backend connections. | By default, each YB-TServer node allows up to 300 connections. You should consider issuing an alert when the total connection count is close to 300*(number of YB-TServers). | ![img](/images/yp/metrics100.png) |

### YCQL ops and latency

| **Graph**             | **Description**                                              | **Alert Guidance**                                           | **Example**                     |
| :-------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ | ------------------------------- |
| Total YCQL Ops / Sec  | The count of DELETE, INSERT, SELECT, and UPDATE transactions, as well as other statements through the YCQL API. | An alert should be issued if count drops significantly lower than your average count, as this could indicate an application connection failure. | ![img](/images/yp/metrics3.png) |
| YCQL Op Latency (Avg) | The average time of DELETE, INSERT, SELECT, and UPDATE transactions, as well as other statements through the YCQL API. | An alert should be issued when latency is close to or higher than your application SLA. | ![img](/images/yp/metrics4.png) |
| YCQL Op Latency (P99) | The average time of the top 99% of DELETE, INSERT, SELECT, and UPDATE transactions, as well as other statements through the YCQL API. | If this value is significantly higher than expected, then it might be a cause for concern and you might want to issue an alert.<br>You should check whether or not there are consistent spikes in latency. | ![img](/images/yp/metrics5.png) |

### Resource

Resource metrics should be considered on a per-node basis.

| Graph                                 | **Description**                                              | **Alert Guidance**                                           | **Example**                      |
| :------------------------------------ | ------------------------------------------------------------ | ------------------------------------------------------------ | -------------------------------- |
| CPU Usage                             | The percentage of CPU utilization on nodes being consumed by the tablet or master server Yugabyte processes, as well as other processes, if any.<br>In general, CPU usage is a measure of all processes running on the server.<br><br>The user CPU time is the amount of time the processor worked on all the processes.<br><br>The system CPU time is the amount of time the processor worked on the operating system's functions connected to the processes. | If you have alerts enabled, you should check  if you received any high CPU utilization alerts. These alerts could indicate a problem and may require debugging by Yugabyte Support. | ![img](/images/yp/metrics6.png)  |
| Memory Usage (Buffered, Cached, Free) | The amount of RAM (in GB) available to the nodes in the cluster.<br><br>The buffered memory is the size of in-memory block input/output buffers.<br><br>The cached memory is the size of the page cache.<br><br>Free memory is not in use. | An alert should not be issued on memory metrics directly. It is recommended to use another metric to troubleshoot the underlying problem causing any memory bottlenecks.<br>The assumption is that the computer is dedicated to Yugabyte processes, therefore Yugabyte processes aggressively use the memory provided for caches and other operations. | ![img](/images/yp/metrics7.png)  |
| Disk IOPS / Node                      | The number of disk input / output read and write operations per second averaged over each node. | Large spikes usually indicate large compactions. Rarely, in cases of a spiky workload, this could indicate block cache misses.<br>Because random reads always hit disk, it is recommended to increase IOPS capacity for this type of workload.<br>You should set an alert to a value much greater than your average or as a percentage of your available IOPS.<br>This value is averaged across all nodes in a cluster. An alert should be issued per node to detect source of underlying issues. | ![img](/images/yp/metrics8.png)  |
| Disk Bytes / Sec / Node               | The number of bytes (scale: millions) being read or written to disk per second, averaged over each node. | If the maximum IOPS for the instance volume type has high utilization, you should ensure that the schema and query are optimized.<br>In addition, consider increasing the instance volume IOPS capacity. | ![img](/images/yp/metrics9.png)  |
| Network Packets / Sec / Node          | The count of network packets received to the server (RX) and transmitted from the server (TX) per second, averaged over nodes. | Provides a view of the intensity of the network activity on the server.<br>This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics10.png) |
| Network Bytes / Sec / Node            | The size (in bytes; scale: millions) of network packets received (RX) and transmitted (TX) per second, averaged over nodes. | Provides a view of the intensity of the network activity on the server.<br/>This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics11.png) |
| Network Errors / Sec / Node           | The number of errors related to network packets received (RX) and transmitted (TX) per second, averaged over nodes. | Generally, you should issue an alert for any error. If your environment produces a lot of errors, that could indicate an underlying infrastructure or operating system issue. | ![img](/images/yp/metrics12.png) |
| System Load Over Time                 | The measure of system load averaged over 1, 5, and 15 minutes. | Values greater than your configured number of cores indicates that processes are waiting for CPU time. It is recommended that you consider your averages when determining the alert threshold.<br>In some cases, this can mean issuing an alert when the 5-minute load average is at 75-80% of available cores on the server. For some systems and workloads, you may want to set the threshold higher (for example, to 4 times the number of cores). | ![img](/images/yp/metrics13.png) |
| Clock Skew                            | For YugabyteDB to preserve data consistency, the clock drift and clock skew across different nodes are configured within bounds. | Clock skew is important for performance and data consistency. An OSS product can refuse to come up or can crash at a default value of 500 milliseconds, as it is considered better to be down than inconsistent.<br>YugabyteDB Anywhere issues alerts at the same threshold, and it should be considered a top priority to resolve this alert. | ![img](/images/yp/metrics14.png) |

### YSQL advanced

| **Graph**             | **Description**                                              | **Alert Guidance**                                           | **Example**                       |
| --------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ | --------------------------------- |
| Total YSQL Ops / sec  | The number of various transaction control operations, such as BEGIN, COMMIT, ROLLBACK, as well as other operations through the YSQL API. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics101.png) |
| YSQL Op Latency (Avg) | The average time taken by various transaction control operations, such as BEGIN, COMMIT, ROLLBACK, as well as other operations through the YSQL API. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics102.png) |

### YCQL advanced

| **Graph**                     | **Description**                                              | **Alert Guidance**                                           | **Example**                      |
| ----------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ | -------------------------------- |
| YCQL Latency Breakdown        | The average time spent by the CQL API parsing and executing operations. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics15.png) |
| YBClient Ops Local vs Remote  | The count of local and remote read and write requests.<br>Local requests are executed on the same node that has received the request.<br>Remote requests are re-routed internally to a different node for executing the operation. | If an application is using a Yugabyte driver that supports local query routing optimization and prepared statements, the expected value for this is close to 100% local for local reads and writes.<br>If using the Cassandra driver or not using prepared statements, expect to see a relatively even split (for example, ~33% local and ~66% remote for a 3-node cluster). | ![img](/images/yp/metrics16.png) |
| YBClient Latency              | Latency of local and remote read and write requests.<br>Refer to the YBClient Ops Local vs Remote description regarding local and remote requests. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics17.png) |
| Reactor Delays                | The number of microseconds the incoming CQL requests spend in the worker queue before the beginning of processing. <br><br>Note that Reactor is a software implementation of a ring queue. | This value should be close to zero. If it is increasing or stays high, you should treat it as an indicator of a network issue or that the queues are full. If this is the case, you should investigate throughput and queue size and latency metrics for tuning guidance. | ![img](/images/yp/metrics18.png) |
| RPC Queue Size                | The number of RPCs in the service queue.                     | The queue size is an indicator of incoming traffic and throughput. Typically, it either shows very low values, which means that YugabyteDB Anywhere is processing requests at a healthy rate, or a flat line at the maximum-configured queue size, which means that YugabyteDB Anywhere has filled the queues and cannot process fast enough. | ![img](/images/yp/metrics19.png) |
| Response Size (bytes)         | The size of the RPC response.                                | The response size for RPCs should be relatively small.<br>This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics20.png) |
| Transaction                   | The number of transactions.                                  | This value depends on the application or activity. Because transactions can have batched statements, it is not possible to provide a specific guidance for this metric, and it is purely informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics21.png) |
| Inbound RPC Connections Alive | The count of current connections at the CQL API level.       | If this spikes to a number much higher than your average, you should consider that there may be an active DDoS or a security incident. | ![img](/images/yp/metrics22.png) |

### Tablet server

| **Graph**                     | **Description**                                              | **Alert Guidance**                                           | **Example**                      |
| ----------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ | -------------------------------- |
| Ops / Sec / Node                     | This is the key throughput measure.<br><br>Read: the count of read operations at the tablet level.<br>Write: the count of write operations at the tablet level. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics23.png)                             |
| Average Latency                      | Read: the average latency of read operations at the tablet level.<br>Write: the average latency of write operations at the tablet level. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics24.png) |
| Reactor Delays                       | The number of microseconds the incoming RPC requests spend in the worker queue before the beginning of processing.<br><br>Note that Reactor is a software implementation of a ring queue. | If this metric spikes or remains at a high level, it indicates a network issue or that the queues are full. | ![img](/images/yp/metrics25.png) |
| Threads Running                      | The current number of running threads. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics103.png) |
| Threads Started | The total number of threads started on this server. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics104.png) |
| Consensus Ops / Sec                  | Yugabyte implements the RAFT consensus protocol, with minor modifications.<br><br><br>Update: replicas implement an RPC method called UpdateConsensus which allows a leader to replicate a batch of log entries to the follower. Only a leader may call this RPC method, and a follower can only accept an UpdateConsensus call with a term equal to or higher than its currentTerm.<br>Request: replicas also implement an RPC method called RequestConsensusVote, which is invoked by candidates to gather votes.<br>MultiRaftUpdates: information pending. | A high number for the Request Consensus indicates that a lot of replicas are looking for a new election because they have not received a heartbeat from the leader.<br>A high CPU or a network partition can cause this condition, and therefore it should result in issuing an alert. | ![img](/images/yp/metrics27.png) |
| Total Consensus Change Config        | This metric is related to the RAFT Consensus Process.<br><br>ChangeConfig: the number of times a peer was added or removed from the consensus group.<br>LeaderStepDown: the number of leader changes.<br>LeaderElectionLost:<br/>the number of times a leader election has failed.<br>RunLeaderElection: the count of leader elections due to a node failure or network partition. | You should issue an alert on LeaderElectionLost.<br>An increase in ChangeConfig typically happens when YugabyteDB Anywhere needs to move data around. This may happen as a result of a planned server addition or decommission, or a server crash looping.<br>A LeaderStepDown can indicate a normal change in leader, or it could be an indicator of a high CPU, blocked RPC queues, server restarts, and so on. You should issue an alert on LeaderStepDown as a proxy for other system issues. | ![img](/images/yp/metrics28.png) |
| Remote Bootstraps                    | The total count of remote bootstraps.                        | When a RAFT peer fails, YugabyteDB executes an automatic remote bootstrap to create a new peer from the remaining ones.<br>Bootstrapping can also be a result of planned user activity when adding or decommissioning nodes.<br>It is recommended to issue an alert on a change in this count outside of planned activity. | ![img](/images/yp/metrics29.png) |
| Consensus RPC Latencies              | RequestConsensus: latency of consensus request operations.<br>UpdateConsensus: latency of consensus update operations.<br>MultiRaftUpdates: information pending. | If the value is high, it is likely that the overall latency is high.<br>This metric should be treated as a starting point in debugging the YB-Master and YB-TServer processes. | ![img](/images/yp/metrics30.png) |
| Change Config Latency                | Latency of consensus change configuration processes. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics31.png) |
| Context Switches                     | Voluntary context switches are writer processes that take a lock.<br>Involuntary context switches happen when a writer process has waited longer than a set threshold, which results in other waiting processes taking over. | A large number of involuntary context switches indicates a CPU-bound workload. | ![img](/images/yp/metrics32.png) |
| Spinlock Time / Server             | Spinlock is a measurement of processes waiting for a server resource and using a CPU to check and wait repeatedly until the resource is available. | This value can become very high on large computers with many cores.<br>The GFlag `tserver_tcmalloc_max_total_thread_cache_bytes` is by default 256 MB, and this is typically sufficient for 16-core computers with less than 32 GB of memory. For larger computers, it is recommended to increase this to 1 GB or 2 GB.<br>You should monitor memory usage, as this requires more memory. | ![img](/images/yp/metrics34.png) |
| WAL Latency    | This is related to WALs.<br><br>Commit (Log Group Commit Latency): the number of microseconds spent on committing an entire group.<br>Append (Log Append Latency): the number of microseconds spent on appending to the log segment file.<br>Sync (Log Sync Latency): the number of microseconds spent on synchronizing the log segment file. | These metrics provide information on the amount to time spent writing to a disk. You should perform tuning accordingly. | ![img](/images/yp/metrics35.png) |
| WAL Bytes Written / Sec / Node | The number of bytes written to the WAL after the tablet start. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics36.png) |
| WAL Bytes Read / Sec / Node    | The number of bytes read from the WAL after the tablet start. | It is recommended to issue an alert on values higher than the normal range. An increase indicates that followers are falling behind and are constantly trying to catch up.<br>In an xCluster replication topology, this can indicate replication latency. | ![img](/images/yp/metrics37.png) |
| WAL Ops / Sec / Node   | This is related to WALs.<br><br>Commit (Log Group Commit Count): the number of commits of an entire group, per second, per node.<br>Append (Log Append Count): the number of appends to the log segment file, per second, per node.<br>Sync (Log Sync Count): the number of syncs for the log segment file, per second, per node. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics38.png) |
| TCMalloc Stats               | In Use (Heap Memory Usage): the number of bytes used by the application. Typically, this does not match the memory use reported by the OS because it does not include TCMalloc overhead or memory fragmentation.<br>Total (Reserved Heap Memory): the number of bytes of the system memory reserved by TCMalloc.<br> | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics.<br><br>Also consider the following:<br>Free Heap Memory: the number of bytes in free, mapped pages in a page heap. These bytes can be used to fulfill allocation requests. They always count towards virtual memory usage, and unless the underlying memory is swapped out by the OS, they also count towards physical memory usage.<br>Unmapped Heap Memory: the number of bytes in free, unmapped pages in a page heap. These are bytes that have been released back to the OS, possibly by one of the MallocExtension Release calls. They can be used to fulfill allocation requests, but typically incur a page fault. They always count towards virtual memory usage, and depending on the OS, usually do not count towards physical memory usage.<br>Thread Cache Memory Limit: a limit to the amount of memory that TCMalloc dedicates for small objects. In some cases, larger numbers trade off more memory use for improved efficiency.<br>Thread Cache Memory Usage: a measure of some of the memory TCMalloc is using (for small objects). | ![img](/images/yp/metrics39.png) |
| WAL Stats / Node               | WAL bytes read: the size (in bytes) of reads from the WAL after the tablet start.<br>WAL cache size: the total per-tablet size of consensus entries kept in memory. The log cache attempts to keep all entries which have not yet been replicated to all followers in memory, but if the total size of those entries exceeds the configured limit in an individual tablet, the oldest is evicted. | If the log cache size is greater than zero, the followers are behind. You should issue an alert if the value spikes or remains above zero for an extended period of time. | ![img](/images/yp/metrics40.png) |
| WAL Cache Num Ops / Node       | The number of times the log cache is accessed.               | Outside of an xCluster replication deployment, a number greater than zero means some of the followers are behind. | ![img](/images/yp/metrics42.png) |
| Glog messages                       | The following log levels are available:<br><br>Info: the number of INFO-level log messages emitted by the application.<br>Warning: the number of WARNING-level log messages emitted by the application.<br>Error: the number of ERROR-level log messages emitted by the application. | It is recommended to use a log aggregation tool for log analysis. You should review the ERROR-level log entries on a regular basis. | ![img](/images/yp/metrics43.png) |
| RPC Queue Size                       | The number of RPCs in service queues for tablet servers:<br><br>CDC service<br>Remote Bootstrap service<br>Consensus service<br>Admin service<br>Generic service<br>Backup service | The queue size is an indicator of the incoming traffic.<br>If the backends get overloaded, requests pile up in the queues. When the queue is full, the system responds with backpressure errors.<br><br>Also consider the following Prometheus metrics:<br>`rpcs_timed_out_in_queue` - the number of RPCs whose timeout elapsed while waiting in the service queue, which resulted in these RPCs not having been processed. This number does not include calls that were expired before an attempt to execute them has been made.<br>`rpcs_timed_out_early_in_queue` - the number of RPCs whose timeout elapsed while waiting in the service queue, which resulted in these RPCs not having been processed. The timeouts for these calls were detected before the calls attempted to execute.<br>`rpcs_queue_overflow` - the number of RPCs dropped because the service queue was full. | ![img](/images/yp/metrics44.png) |
| CPU Util Secs / Sec  | The tablet server CPU utilization.                           | The tablet server should not use the full allocation of CPUs. For example, on a 4-core computer, three cores are used by the tablet server, but if the usage is usually close to three, you should increase the number of available CPUs. | ![img](/images/yp/metrics45.png) |
| Inbound RPC Connections Alive        | The count of active connections to YB-TServers.      | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics46.png) |

### Master server

| **Graph**                           | **Description**                                              | **Alert Guidance**                                           | **Example**                      |
| ----------------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ | -------------------------------- |
| Overall RPCs / sec                  | The number of created RPC inbound calls to the master servers. | The limit is 1000 TPS on the master, but under normal circumstances this number should be much lower than the limit.<br>It is recommended to issue an alert at a range under the limit, yet higher than your average. | ![img](/images/yp/metrics47.png) |
| Average Latency                     | The average latency of various RPC services, provided by Master server | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics76.png) |
| GetTabletLocations / sec            | The number of times the locations of the replicas for a given tablet were fetched from the master servers. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics48.png) |
| Master TSService Reads / sec        | The measure of YSQL reads of the PostgreSQL system tables.     | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics50.png) |
| Master TSService Reads Latency      | The average latency of YSQL reads of the PostgreSQL system tables. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics77.png) |
| Master TSService Writes / sec       | The measure of YSQL writes to the PostgreSQL system tables (during DDL). | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics51.png) |
| Master TSService Writes Latency     | The average latency of YSQL writes to the PostgreSQL system tables (during DDL). | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics78.png) |
| TS Heartbeats / sec                 | The count of heartbeats received by the master server leader from the tablet servers. This establishes liveness and reports back any status changes. | This measure can be used to determine which master is the leader, as only the leader gets active heartbeats. | ![img](/images/yp/metrics52.png) |
| RPC Queue Size                      | The number of RPCs in a service queue for master servers.    | The queue size is an indicator of the incoming traffic.<br><br>This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics53.png) |
| UpdateConsensus / sec               | The number of consensus update operations.                   | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics54.png) |
| UpdateConsensus Latency             | Latency of consensus update operations.                      | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics79.png) |
| MultiRaftUpdateConsensus  / sec     | The number of multiple Raft group consensus update operations. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics80.png) |
| MultiRaftUpdateConsensus Latency    | Latency of multiple Raft group consensus update operations.  | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics81.png) |
| Create / Delete Table RPCs          | The count of administrative operations CreateTable and DeleteTable. | A deletion can fail when other operations, such as bootstrap, are running. This uses a configured retry timeout. | ![img](/images/yp/metrics55.png) |
| CPU Util Secs / Sec (Master Server) | The master server CPU utilization.                           | The master server should not use a full CPU.                 | ![img](/images/yp/metrics56.png) |
| Inbound RPC Connections Alive       | The count of active connections to master servers.           | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics57.png) |

### Master server advanced

| **Graph**                      | **Description**                                              | **Alert Guidance**                                           | **Example**                      |
| ------------------------------ | ------------------------------------------------------------ | ------------------------------------------------------------ | -------------------------------- |
| Threads Running                | The current number of running threads.                       | This metric is  informational and should not be subject to alerting. You may consider  this information while examining alerts on other metrics. | ![img](/images/yp/metrics83.png) |
| WAL Latency                    | This is related to WALs.<br><br/>Commit (Log Group Commit Latency): the number of microseconds spent on committing an entire group.<br/>Append (Log Append Latency): the number of microseconds spent on appending to the log segment file.<br/>Sync (Log Sync Latency): the number of microseconds spent on synchronizing the log segment file. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics84.png) |
| WAL Bytes Written / Sec / Node | The number of bytes written to the WAL after the tablet start. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics85.png) |
| WAL Bytes Read / Sec / Node    | The number of bytes read from the WAL after the tablet start. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics86.png) |
| TCMalloc Stats                 | In Use (Heap Memory  Usage): the number of bytes used by the application. Typically, this  does not match the memory use reported by the OS because it does not  include TCMalloc overhead or memory fragmentation.<br/>Total (Reserved Heap Memory): the number of bytes of the system memory reserved by TCMalloc. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics87.png) |
| Glog Messages                  | The following log levels are available:<br/><br/>Info: the number of INFO-level log messages emitted by the application.<br/>Warning: the number of WARNING-level log messages emitted by the application.<br/>Error: the number of ERROR-level log messages emitted by the application. | It is recommended to use a log aggregation tool for log analysis. You should review the ERROR-level log entries on a regular basis. | ![img](/images/yp/metrics88.png) |
| LSM-DB Seek / Next Num Ops     | The number of calls to seek / next.                          | Alerting is not required.                                    | ![img](/images/yp/metrics89.png) |
| LSM-DB Seeks / sec             | The number of calls to seek per second.                      | Alerting is not required.                                    | ![img](/images/yp/metrics90.png) |
| SSTable size                   | The size (in bytes) of all SST files.                        | Alerting is not required.                                    | ![img](/images/yp/metrics91.png) |
| Cache Hit & Miss               | Hit: the total number of block cache hits (cache index + cache filter + cache data).<br/>Miss: the total number of block cache misses (cache index + cache filter + cache data). | If the number of misses is significant, it is recommended to issue an alert. | ![img](/images/yp/metrics92.png) |
| Block cache usage              | A block requires multiple touches before it is added to the multi-touch (hot) portion of the cache.<br/><br/>Multi Touch: the size (in bytes) of the cache usage by blocks having multiple touches.<br/>Single Touch: the size (in bytes) of the cache usage by blocks having only a single touch. | Alerting is not required.                                    | ![img](/images/yp/metrics93.png) |
| LSM-DB Blooms usefulness       | Blooms checked: the number of times the bloom filter has been checked.<br/>Blooms useful: the number of times the bloom filter has avoided file reads (avoiding iops). | Alerting is not required.                                    | ![img](/images/yp/metrics94.png) |
| Flush write                    | The number of bytes written during the flush process.        | Alerting is not required.                                    | ![img](/images/yp/metrics95.png) |
| Compaction                     | The number of bytes being read and written to do compaction. If not a lot of data is being deleted, these levels are similar. In some cases, you might see a large delete indicated by large reads but low writes afterwards (because a large percentage of data was removed in compaction). | Alerting is not required.                                    | ![img](/images/yp/metrics96.png) |
| Compaction time                | Time (in milliseconds) for the compaction processes to complete. | Alerting is not required.                                    | ![img](/images/yp/metrics97.png) |
| Compaction num files           | The average number of files in any single compaction.        | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics98.png) |

### DocDB

DocDB uses a highly customized version of [RocksDB](http://rocksdb.org/), a log-structured merge tree (LSM)-based key-value store. The majority of the following metrics are used internally by engineers when troubleshooting a deployment, and, therefore, should not be subject to alerting.

| **Graph**                   | **Description**                                              | **Alert Guidance**                                           | **Example**                       |
| --------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ | --------------------------------- |
| LSM-DB Seek / Next Num Ops  | The number of calls to seek / next.                          | Alerting is not required.                                    | ![img](/images/yp/metrics58.png)  |
| LSM-DB Seeks / Sec / Node   | The number of calls to seek per second per node.             | Alerting is not required.                                    | ![img](/images/yp/metrics59.png)  |
| SSTable size / Node         | The size (in bytes) of all SST files.                        | Alerting is not required.                                    | ![img](/images/yp/metrics60.png)  |
| Average SSTables / Node     | The average number of SST files across nodes.                | Alerting is not required.                                    | ![img](/images/yp/metrics61.png)  |
| LSM-DB Get Latencies        | Latency in time to retrieve data matching a value.           | Alerting is not required.                                    | ![img](/images/yp/metrics62.png)  |
| LSM-DB Write Latencies      | Latency in time to write data.                               | Alerting is not required.                                    | ![img](/images/yp/metrics63.png)  |
| LSM-DB Seek Latencies       | Latency in time to retrieve data in a range query.           | Alerting is not required.                                    | ![img](/images/yp/metrics64.png)  |
| LSM-DB Mutex Wait Latencies | The wait time for the DB mutex. This mutex is held for meta operations, such as checking data structures before and after compactions or flushes. | Alerting is not required.                                    | ![img](/images/yp/metrics65.png)  |
| Cache Hit & Miss            | Hit: the total number of block cache hits (cache index + cache filter + cache data).<br>Miss: the total number of block cache misses (cache index + cache filter + cache data). | If the number of misses is significant, it is recommended to issue an alert. | ![img](/images/yp/metrics66.png)  |
| Block cache usage           | A block requires multiple touches before it is added to the multi-touch (hot) portion of the cache.<br><br>Multi Touch: the size (in bytes) of the cache usage by blocks having multiple touches.<br>Single Touch: the size (in bytes) of the cache usage by blocks having only a single touch. | Alerting is not required.                                    | ![img](/images/yp/metrics67.png)  |
| LSM-DB Blooms usefulness    | Blooms checked: the number of times the bloom filter has been checked.<br>Blooms useful: the number of times the bloom filter has avoided file reads (avoiding iops). | Bloom filters are hash tables used to determine if a given sstable has the data for a query looking for a particular value.<br>Bloom filters are not helpful for range queries, and therefore alerting is not required. | ![img](/images/yp/metrics68.png)  |
| Stalls                      | Time the writer has to wait for compactions or flushes to finish. | Alerting is not required                                     | ![img](/images/yp/metrics69.png)  |
| Rejections                  | The number of RPC requests rejected due to the number of majority SST files. | Rejections can happen due to hitting the soft limit on SST files, hitting more than the soft memory limit (set to 85% of the hard limit), and so on.<br>Alerting is not required. | ![img](/images/yp/metrics70.png)  |
| Flush write                 | The number of bytes written during the flush process.        | Alerting is not required.                                    | ![img](/images/yp/metrics71.png)  |
| Compaction                  | The number of bytes being read and written to do compaction. If not a lot of data is being deleted, these levels are similar. In some cases, you might see a large delete indicated by large reads but low writes afterwards (because a large percentage of data was removed in compaction). | Alerting is not required.                                    | ![img](/images/yp/metrics72.png)  |
| Compaction time             | Time for the compaction processes to complete.               | Alerting is not required.                                    | ![img](/images/yp/metrics73.png)  |
| Compaction num files        | The average number of files in any single compaction.        | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics74.png)  |
| Transaction                 | Expired Transactions: the number of expired distributed transactions.<br>Transaction Conflicts: the number of conflicts detected among uncommitted distributed transactions.<br><br>This is related to the process that resolves conflicts for write transactions. This process reads all intents that could conflict and tries to abort transactions with a lower priority. If a write transaction conflicts with a higher-priority transaction, then an error is returned and this metric is iterated. | Alerting is not required.                                    | ![img](/images/yp/metrics75.png)  |
| Transaction pool cache hits | Percentage of transaction pool requests fulfilled by the transaction pool cache. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | ![img](/images/yp/metrics105.png) |

<!--

### Replication

| **Graph**             | **Description**                                              | **Alert Guidance**                                           | **Example**     |
| --------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ | --------------- |
| Async Replication Lag | The maximum lag across all tables in an xCluster replication deployment.<br>This time represents a delay from a Producer universe to a Consumer universe.<br>The value of this metric is only shown on the Producer universe, unless there is a bi-directional replication. | This metric is informational and should not be subject to alerting. You may consider this information while examining alerts on other metrics. | Example pending |

-->

### Command-line access to metrics

YugabyteDB Anywhere allows you to access all metrics via the command-line interface (CLI), including those not available from the **Metrics** page.

You can view YB-TServer and YB-Master server metrics in [Prometheus](https://prometheus.io/) and JSON formats in the browser or via the CLI using curl commands.

The following is the Prometheus command for the YB-TServer:

```sh
curl <node_IP>:9000/prometheus-metrics
```

Expect an output similar to the following:

![Prometheus](/images/yp/metrics-prometheus.png)

The following is the Prometheus command for the YB-Master:

```sh
curl <node_IP>:7000/prometheus-metrics
```

The following is the JSON command for the YB-TServer:

```sh
curl <node_IP>:9000/metrics
```

Expect an output similar to the following:

![JSON](/images/yp/metrics-json.png)

The following is the JSON command for the YB-Master:

```sh
curl <node_IP>:7000/metrics
```

You can obtain additional information if you request the schema in the metrics URL, as follows:

```sh
curl <node_IP>:9000/metrics?include_schema=true
```

```sh
curl <node_IP>:7000/metrics?include_schema=true
```

The output would be similar to the following:

```json
[
    {
        "type": "tablet",
        "id": "aaec98a6a44a418999f325d637f9e8cf",
        "attributes": {
            "partition": "hash_split: [0xBFFA, 0xDFF9)",
            "namespace_name": "mykeyspace",
            "table_name": "foo",
            "table_id": "63e3fee5f5f441eeb5a5071ebd64c232"
        },
        "metrics": [
            {
                "name": "mem_tracker_log_cache",
                "label": "Memory consumed by log_cache-aaec98a6a44a418999f325d637f9e8cf->log_cache->server->root",
                "type": "gauge",
                "unit": "bytes",
                "description": "Memory consumed by log_cache-aaec98a6a44a418999f325d637f9e8cf->log_cache->server->root",
                "level": "info",
                "value": 0
            },
            {
                "name": "mem_tracker_RegularDB_MemTable",
                "label": "Memory consumed by MemTable->RegularDB->tablet-aaec98a6a44a418999f325d637f9e8cf->Tablets->server->root",
                "type": "gauge",
                "unit": "bytes",
                "description": "Memory consumed by MemTable->RegularDB->tablet-aaec98a6a44a418999f325d637f9e8cf->Tablets->server->root",
                "level": "info",
                "value": 2048
            },
...
```

{{< note title="Note" >}}

YugabyteDB Anywhere obtains the replication lag using Prometheus metrics from the YB-TServer port 9000. If this port is closed, the xCluster replication still works, but the replication lag would not be available via **Metrics**.

{{< /note >}}

You can also federate metrics from YugabyteDB Anywhere and configure alerting rules to trigger alerts in Prometheus. For details, see the following sections of [Alerts and Notifications in YugabyteDB Anywhere](https://www.yugabyte.com/blog/yugabytedb-2-8-alerts-and-notifications/):

- Scrape metrics from YugabyteDB Anywhere
- Configurable alerting rules

## Use nodes status

You can check the status of the YB-Master and YB-TServer on each YugabyteDB node by navigating to **Universes > Universe-Name > Nodes**, as per the following illustration:

![Node Status](/images/yp/troubleshoot-node-status.png)

If issues arise, additional information about each master and YB-TServer is available on their respective **Details** pages, or by accessing `<node_IP>:7000` for YB-Master servers and `<node_IP>:9000` for YB-TServers (unless the configuration of your on-premises data center or cloud-provider account prevents the access, in which case you may consult [Check YugabyteDB servers](../../../troubleshoot/nodes/check-processes/)).

## Check host resources on the nodes

To check host resources on your YugabyteDB nodes, run the following script, replacing the IP addresses with the IP addresses of your YugabyteDB nodes:

```sh
for IP in 10.1.13.150 10.1.13.151 10.1.13.152; \
do echo $IP; \
  ssh $IP \
    'echo -n "CPUs: ";cat /proc/cpuinfo | grep processor | wc -l; \
      echo -n "Mem: ";free -h | grep Mem | tr -s " " | cut -d" " -f 2; \
      echo -n "Disk: "; df -h / | grep -v Filesystem'; \
done
```

The output should look similar to the following:

```output
10.1.12.103
CPUs: 72
Mem: 251G
Disk: /dev/sda2       160G   13G  148G   8% /
10.1.12.104
CPUs: 88
Mem: 251G
Disk: /dev/sda2       208G   22G  187G  11% /
10.1.12.105
CPUs: 88
Mem: 251G
Disk: /dev/sda2       208G  5.1G  203G   3% /
```

## Troubleshoot universe creation

You typically create universes by navigating to **Universes > Create universe > Primary cluster**, as per the following illustration:

![Troubleshoot universe](/images/yb-platform/troubleshoot-uni-creation.png)<br>

If you disable **Assign Public IP** during the universe creation, the process may fail under certain conditions, unless you either install the following packages on the machine image or make them available on an accessible package repository:

- `chrony`, if you enabled **Use Time Sync** for the selected cloud provider.
- `python-minimal`, if YugabyteDB Anywhere is installed on Ubuntu 18.04.
- `python-setuptools`, if YugabyteDB Anywhere is installed on Ubuntu 18.04.
- `python-six` or `python2-six` (the Python2 version of Six).
- `policycoreutils-python`, if YugabyteDB Anywhere is installed on CentOS 7 or Oracle Linux 8.
- `selinux-policy` must be on an accessible package repository, if YugabyteDB Anywhere is installed on Oracle Linux 8.
- `locales`, if YugabyteDB Anywhere is installed on Ubuntu.

The preceding package requirements are applicable to YugabyteDB Anywhere version 2.13.1.0.

If you are using YugabyteDB Anywhere version 2.12.*n*.*n* and disable **Use Time Sync** during the universe creation, you also need to install the `ntpd` package.

## Use support bundles

A support bundle is an archive generated at a universe level. It contains all the files required for diagnosing and troubleshooting a problem. The diagnostic information is provided by the following types of files:

- Application logs from YugabyteDB Anywhere.
- Universe logs, which are the YB-Master and YB-TServer log files from each node in the universe, as well as PostgreSQL logs available under the YB-TServer logs directory.
- Output files ( `.out` ) files generated by the YB-Master and YB-TServer.
- Error files ( `.err` ) generated by the YB-Master and YB-TServer.
- G-flag configuration files containing the gflags set on the universe.
- Instance files that contain the metadata information from the YB-Master and YB-TServer.
- Consensus meta files containing consensus metadata information from the YB-Master and YB-TServer.
- Tablet meta files containing the tablet metadata from the YB-Master and YB-TServer.

The diagnostic information can be analyzed locally or the bundle can be forwarded to the Yugabyte Support team.

You can create a support bundle as follows:

- Open the universe that needs to be diagnosed and click **Actions > Support Bundles**.

- If the universe already has support bundles, they are displayed by the **Support Bundle** dialog. If there are no bundles for the universe, use the **Support Bundles** dialog to generate a bundle by clicking **Create Support Bundle**, as per the following illustration:

  ![Create support bundle](/images/yp/support-bundle-1.png)

- Select the date range and the types of files to be included in the bundle, as per the following illustration:

  ![Create support bundle](/images/yp/support-bundle-2.png)

- Click **Create Bundle**.

  YugabyteDB Anywhere starts collecting files from all the nodes in the cluster into an archive. Note that this process might take several minutes. When finished, the bundle's status is displayed as **Ready**, as per the following illustration:

  ![Create support bundle](/images/yp/support-bundle-3.png)

  The **Support Bundles** dialog allows you to either download the bundle or delete it if it is no longer needed. By default, bundles expire after ten days to free up space.

## Debug crashing YugabyteDB pods in Kubernetes

If the YugabyteDB pods of your universe are crashing, you can debug them with the help of following instructions.

### Collect core dumps in Kubernetes environments

When dealing with Kubernetes-based installations of YugabyteDB Anywhere, you might need to retrieve core dump files in case of a crash within the Kubernetes pod. For more information, see [Specify ulimit and remember the location of core dumps](../../install-yugabyte-platform/prepare-environment/kubernetes/#specify-ulimit-and-remember-the-location-of-core-dumps).

The process of collecting core dumps depends on the value of the sysctl `kernel.core_pattern`, which you can inspect within a Kubernetes pod or node by executing the following command:

```sh
cat /proc/sys/kernel/core_pattern
```

The value of `core_pattern` can be a literal path or it can contain a pipe symbol:

- If the value of `core_pattern` is a literal path of the form `/var/tmp/core.%p`, cores are copied by the YugabyteDB node to a persistent volume directory that you can inspect using the following command:

  ```sh
  kubectl exec -it -n <namespace> <pod_name> -c yb-cleanup -- ls -lht /var/yugabyte/cores
  ```

  In the preceding command, the `yb-cleanup` container of the node is used because the primary YB-Master or YB-TServer container may be in a crash loop.

  To copy a specific core dump file at this location, use the following kubectl `cp` command:

  ```sh
  kubectl cp -n <namespace> -c yb-cleanup <yb_pod_name>:/var/yugabyte/cores/core.2334 /tmp/core.2334
  ```

- If the value of `core_pattern` contains a `|` pipe symbol (for example, `|/usr/share/apport/apport -p%p -s%s -c%c -d%d -P%P -u%u -g%g -- %E`), the core dump is being redirected to a specific collector on the underlying Kubernetes node, with the location depending on the exact collector. In this case, it is your responsibility to identify the location to which these files are written and retrieve them.

### Use debug hooks with YugabyteDB in Kubernetes

You can add your own commands to pre- and post-debug hooks to troubleshoot crashing YB-Master or YB-TServer pods. These commands are run before the database process starts and after the database process terminates or crashes.

For example, to modify the debug hooks of a YB-Master, run following command:

```sh
kubectl edit configmap -n <namespace> ybuni1-asia-south1-a-lbrl-master-hooks
```

This opens the configmap YAML in your editor.

To add multiple commands to the pre-debug hook of `yb-master-0`, you can modify the `yb-master-0-pre_debug_hook.sh` key as follows:

```yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: ybuni1-asia-south1-a-lbrl-master-hooks
data:
  yb-master-0-post_debug_hook.sh: 'echo ''hello-from-post'' '
  yb-master-0-pre_debug_hook.sh: |
    echo "Running the pre hook"
    du -sh /mnt/disk0/yb-data/
    sleep 5m
    # other commands here…
  yb-master-1-post_debug_hook.sh: 'echo ''hello-from-post'' '
  yb-master-1-pre_debug_hook.sh: 'echo ''hello-from-pre'' '
```

After you save the file, the updated commands will be executed on the next restart of `yb-master-0`.

You can run the following command to check the output of your debug hook:

```sh
kubectl logs -n <namespace> ybuni1-asia-south1-a-lbrl-yb-master-0 -c yb-master
```

Expect an output similar to the following:

```output
...
2023-03-29 06:40:09,553 [INFO] k8s_parent.py: Executing operation: ybuni1-asia-south1-a-lbrl-yb-master-0_pre_debug_hook filepath: /opt/debug_hooks_config/yb-master-0-pre_debug_hook.sh
2023-03-29 06:45:09,627 [INFO] k8s_parent.py: Output from hook b'Running the pre hook\n44M\t/mnt/disk0/yb-data/\n'
```

## Perform the follower lag check during upgrades

You can use the follower lag check to ensure that the YB-Master and YB-TServer process is caught up to its peers. To find this metric on Prometheus, execute the following:

```sh
max by (instance) (follower_lag_ms{instance='<ip>:<http_port>'})
```

- *ip* represents the YB-Master IP or the YB-TServer IP.
- *http_port* represents the HTTP port on which the YB-Master or YB-TServer is listening. The YB-Master default port is 7000 and the YB-TServer default port is 9000.

The result is the maximum follower lag, in milliseconds, of the most recent Prometheus of the specified YB-Master or YB-TServer process.

Typically, the maximum follower lag of a healthy universe is a few seconds at most. The following reasons may contribute to a significant increase in the follower lag, potentially reaching several minutes:

- Node issues, such as network problems between nodes, an unhealthy state of nodes, or inability of the node's YB-Master or YB-TServer process to properly serve requests. The lag usually persists until the issue is resolved.
- Issues during a rolling upgrade, when the YB-Master or YB-TServer process is stopped, upgrade on the associated process is performed, then the process is restarted. During the downtime, writes to the database continue to occur, but the associated YB-Master or YB-TServer are left behind. The lag gradually decreases after the YB-Master or YB-TServer has restarted and can serve requests again. However, if an upgrade is performed on a universe that is not in a healthy state to begin with (for example, a node is down or is experiencing an unexpected problem), a failure is likely to occur due to the follower lag threshold not being reached within the specified timeframe after the processes have restarted. Note that the default value for the follower lag threshold is 1 minute and the overall time allocated for the process to catch up is 15 minutes. To remedy the situation, perform the following:
  - Bring the node back to a healthy state by stopping and restarting the node, or removing it and adding a new one).
  - Ensure that the YB-Master and YB-TServer processes are running correctly on the node.

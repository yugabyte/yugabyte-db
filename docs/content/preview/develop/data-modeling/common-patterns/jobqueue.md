---
title: Distributed Jobqueue data model
headerTitle: Distributed Jobqueue data model
linkTitle: Jobqueue
description: Explore the Jobqueue data model
headcontent: Understand how to design a distributed jobqueue
badges: ysql
menu:
  preview:
    identifier: common-patterns-jobqueue
    parent: common-patterns
    weight: 300
type: docs
---

Designing a distributed job queue is essential for managing and executing tasks across multiple systems efficiently.  In this guide, we'll walk you through the process of creating a scalable and fault-tolerant job processing system covering design considerations, use cases, and best practices to help you build robust job queues for your applications.

## Use cases

Here are some common use cases for job queues:

- **Background Processing** : Offloading time-consuming tasks, such as sending emails, image processing, or report generation, from the main application flow.
- **Task Scheduling** : Scheduling jobs to be executed at a specific time or interval, such as nightly backups, batch processing, or periodic data cleanups.
- **Distributed Task Execution** : Running tasks across multiple machines or services in a distributed environment. Examples include data processing pipelines, distributed computation, or microservices communication.
- **Error Handling and Retries** : Automatically retrying failed jobs or moving them to a dead-letter queue for later inspection and resolution.

## Setup

<!-- begin: nav tabs -->
{{<nav/tabs list="local,anywhere,cloud" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
{{<setup/local numnodes="3" rf="3">}}
{{</nav/panel>}}

{{<nav/panel name="anywhere">}} {{<setup/anywhere>}} {{</nav/panel>}}
{{<nav/panel name="cloud">}} {{<setup/cloud>}} {{</nav/panel>}}
{{</nav/panels>}}
<!-- end: nav tabs -->

## Schema design

We need to store the information about the jobs. Each job will have its own ID, creation timestamp, current state (e.g., Queued, Running, Completed), and associated data.

For this exercise, we will keep completed jobs in the queue, marked with a status of `Success`, `Failed`, or `Canceled`. To store the status of the job, let's use an enum. To ensure that the job is automatically queued when it is added to the table, we need to set the default value of the status column to be  `Queued`.

{{<note>}}
The job data itself will be distributed based on the `ID` of the job.
{{</note>}}

We need to be able to get the head of the queue efficiently. For this we need to ensure that the we are able to retrieve the earliest inserted job based on the create timestamp efficiently. We will later create an index based on the create timestamp but to help that index to be distributed properly and avoid hotspots , we will create virtual buckets.

{{<tip>}}
For more information on avoiding hotspots, see [Avoid Hotspot on Range-Based Indexes](https://www.yugabyte.com/blog/distributed-databases-hotspots-range-based-indexes/)
{{</tip>}}

```sql
-- Helper enum
CREATE TYPE JobStatus AS ENUM ('Queued','Running','Canceled','Failed','Success');

CREATE TABLE jobs (
  id BIGINT,
  create_ts TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  status JobStatus DEFAULT 'Queued',
  data TEXT,
  bucket smallint DEFAULT floor(random()*4),

  PRIMARY KEY (id)
);
```

## Dummy data

For illustration, lets add a million jobs into the table.

```sql
INSERT INTO Jobs(id, create_ts, data)
 SELECT n, now() - make_interval(mins=>1000000-n, secs=>EXTRACT (SECONDS FROM now())), 'iteminfo-' || n FROM generate_series(1, 1000000) n;
```

The inserted data would look like,

```caddyfile{.nocopy}
   id   |      create_ts      | status |      data       | bucket
--------+---------------------+--------+-----------------+--------
   4443 | 2022-11-02 01:18:00 | Queued | iteminfo-4443   |      1
  17195 | 2022-11-10 20:50:00 | Queued | iteminfo-17195  |      3
  22363 | 2022-11-14 10:58:00 | Queued | iteminfo-22363  |      3
 102397 | 2023-01-09 00:52:00 | Queued | iteminfo-102397 |      1
 148060 | 2023-02-09 17:55:00 | Queued | iteminfo-148060 |      0
```

## Retrieving the head of the Queue

To begin processing the Jobs in sequence, we need to identify the earliest inserted item. This is essentially the head of the queue – the top element when the queue jobs are sorted by create_ts. To do this, we require an index in ascending order of create_ts. Additionally, we only want to retrieve the queued jobs that have yet to be processed. Let’s create an index on status and create_ts.

```sql
CREATE INDEX idx ON jobs (bucket HASH, status, create_ts ASC) INCLUDE(id);
```

{{<note>}}
We have added the id column in the INCLUDE clause to fetch it with an Index-Only scan (without making a trip to the Jobs table).
{{</note>}}

This index is  distributed based on the virtual buckets but the data within each bucket would be ordered based on the create timestamp. To efficiently retrieve the earliest job inserted, we need to create a view as

```sql
CREATE OR REPLACE VIEW jobqueue AS
    (select id,create_ts from Jobs where
          bucket = 0 and status = 'Queued' order by create_ts ASC) union all
    (select id,create_ts from Jobs where
          bucket = 1 and status = 'Queued' order by create_ts ASC) union all
    (select id,create_ts from Jobs where
          bucket = 2 and status = 'Queued' order by create_ts ASC) union all
    (select id,create_ts from Jobs where
          bucket = 3 and status = 'Queued' order by create_ts ASC)
    order by create_ts ASC ;
```

{{<tip>}}
To get a detailed understanding of how this view operates, see [Optimal Pagination for Distributed, Ordered Data](https://www.yugabyte.com/blog/optimize-pagination-distributed-data-maintain-ordering/)
{{</tip>}}

With the help of the above view, we should be able to get the unprocessed Jobs in the required order as,

```sql
select * from jobqueue limit 1;
```

Effectively , the jobqueue will return the inserted data as,

```caddyfile{.nocopy}
 id |      create_ts
----+---------------------
  1 | 2022-10-29 23:16:00
  2 | 2022-10-29 23:17:00
  3 | 2022-10-29 23:18:00
  4 | 2022-10-29 23:19:00
  5 | 2022-10-29 23:20:00
```

## Item selection

We now have a mechanism to fetch jobs from the queue in a distributed, correct, and first-in-first-out (FIFO) manner. To do this, we have to pick the first item in the queue and quickly mark it as selected so that other workers accessing the queue won’t process that particular item.

In YugabyteDB, individual statements are transactional, so we just need to select one item, mark it as Running in one statement, and return the ID selected. For this we can use,

```plpgsql
UPDATE Jobs SET status = 'Running'
WHERE id = (SELECT id FROM jobqueue FOR UPDATE SKIP LOCKED LIMIT 1)
RETURNING id;
```

This selects the item to be picked, quickly marks it as Running, and returns the item-id.

{{<note>}}
During execution, that row will be temporarily locked, so other workers don’t pick it. At the same time, other workers might try to pick the row and this would lead to contention.  To mitigate this, YugabyteDB supports FOR UPDATE SKIP LOCKED.  This lets other worker threads quickly skip over the temporarily locked row, instead of being blocked.
{{</note>}}

## Tombstone problem

Over a period of time, the queue could become slower because of ordered deletes of the data. Although YugabyteDB has optimizations to identify this scenario and automatically trigger smart compaction, it is advisable to store the timestamp of the last processed job and give it as a hint to the executor to retrieve the head of the queue.

```sql
select * from jobqueue where create_ts > '2023-07-20 06:41:00' limit 1;
```

{{<tip>}}
To understand the tombstone problem, see [The Curious Case of the Ever-Increasing Tombstones](https://www.yugabyte.com/blog/keep-tombstones-data-markers-slowing-scan-performance/#houston-we-have-a-problem)
{{</tip>}}

## Final thoughts

By leveraging YugabyteDB’s fault tolerance, high availability, and horizontal scalability, you can build a job queue that ensures reliable task processing, even in complex, multi-node systems. From background task execution to parallel processing and error handling, this approach helps streamline workflows, distribute workloads, and improve the overall resilience of your system. With careful design and implementation, your job queue can scale alongside your application, ensuring smooth and uninterrupted operation.

## Learn more

- [How to Build a Robust Distributed FIFO Job Queue](https://www.yugabyte.com/blog/distributed-fifo-job-queue/)
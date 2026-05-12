---
title: Distributed job queue data model
headerTitle: Distributed job queue data model
linkTitle: Job queue
description: Explore the Job queue data model
headcontent: Understand how to design a distributed job queue
tags:
  other: ysql
menu:
  stable_develop:
    identifier: common-patterns-jobqueue
    parent: common-patterns
    weight: 300
type: docs
---

Designing a distributed job queue is essential for managing and executing tasks across multiple systems efficiently. By leveraging YugabyteDB's fault tolerance, high availability, and horizontal scalability, you can build a job queue that ensures reliable task processing, even in complex, multi-node systems.

Common use cases for job queues include the following:

- Background processing - Offload time-consuming tasks, such as sending emails, image processing, or report generation, from the main application flow.
- Task scheduling - Schedule jobs to be executed at a specific time or interval, such as nightly backups, batch processing, or periodic data cleanups.
- Distributed task execution - Run tasks across multiple machines or services in a distributed environment. Examples include data processing pipelines, distributed computation, or microservices communication.
- Error handling and retries - Automatically retry failed jobs or move them to a dead-letter queue for later inspection and resolution.

From background task execution to parallel processing and error handling, creating a distributed job queue helps streamline and distribute workflows, and improves the overall resilience of your system. With careful design and implementation, your job queue can scale with your application, ensuring smooth, uninterrupted operation.

This guide describes how to create a scalable and fault-tolerant job processing system, and covers design considerations, use cases, and best practices to help you build robust job queues for your applications.

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

You need to store the information about the jobs. Each job will have its own ID, creation timestamp, current state (for example, Queued, Running, Completed), and associated data.

For this exercise, completed jobs are kept in the queue, marked with a status of `Success`, `Failed`, or `Canceled`. To store the status of the job, use an enum. To ensure that the job is automatically queued when it is added to the table, set the default value of the status column to be  `Queued`.

{{<note>}}
The job data itself will be distributed based on the `ID` of the job.
{{</note>}}

You need to be able to get the head of the queue efficiently. To do this, you need to ensure you can retrieve the earliest inserted job based on the create timestamp efficiently.

Later, you'll create an index based on the create timestamp, but so that index can be distributed properly and avoid hot spots, you will create virtual buckets.

{{<tip>}}
For more information on avoiding hot spots, see [How to Avoid Hotspots on Range-based Indexes in Distributed Databases](https://www.yugabyte.com/blog/distributed-databases-hotspots-range-based-indexes/).
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

For illustration, add a million jobs into the table.

```sql
INSERT INTO Jobs(id, create_ts, data)
 SELECT n, now() - make_interval(mins=>1000000-n, secs=>EXTRACT (SECONDS FROM now())), 'iteminfo-' || n FROM generate_series(1, 1000000) n;
```

The inserted data looks like the following:

```caddyfile{.nocopy}
   id   |      create_ts      | status |      data       | bucket
--------+---------------------+--------+-----------------+--------
   4443 | 2022-11-02 01:18:00 | Queued | iteminfo-4443   |      1
  17195 | 2022-11-10 20:50:00 | Queued | iteminfo-17195  |      3
  22363 | 2022-11-14 10:58:00 | Queued | iteminfo-22363  |      3
 102397 | 2023-01-09 00:52:00 | Queued | iteminfo-102397 |      1
 148060 | 2023-02-09 17:55:00 | Queued | iteminfo-148060 |      0
```

## Retrieve the head of the queue

To begin processing the jobs in sequence, you need to identify the earliest inserted item. This is essentially the head of the queue – the top element when the queue jobs are sorted by `create_ts`. To do this, you need an index in ascending order of `create_ts`. Additionally, you only want to retrieve the queued jobs that have yet to be processed.

Create an index on `status` and `create_ts`.

```sql
CREATE INDEX idx ON jobs (bucket HASH, status, create_ts ASC) INCLUDE(id);
```

{{<note>}}
The `id` column is added in the INCLUDE clause so that you can fetch it with an Index-Only scan (without making a trip to the Jobs table).
{{</note>}}

This index is distributed based on the virtual buckets, but the data in each bucket is ordered based on the create timestamp. To efficiently retrieve the earliest job inserted, create a view as follows:

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
For a detailed understanding of how this view operates, see [Optimal Pagination for Distributed, Ordered Data](https://www.yugabyte.com/blog/optimize-pagination-distributed-data-maintain-ordering/).
{{</tip>}}

Using the `jobqueue` view, you can retrieve the unprocessed jobs in the required order as follows:

```sql
select * from jobqueue limit 1;
```

Effectively , `jobqueue` returns the inserted data as follows:

```caddyfile{.nocopy}
 id |      create_ts
----+---------------------
  1 | 2022-10-29 23:16:00
  2 | 2022-10-29 23:17:00
  3 | 2022-10-29 23:18:00
  4 | 2022-10-29 23:19:00
  5 | 2022-10-29 23:20:00
```

## Fetch jobs from the queue

You now have a mechanism to fetch jobs from the queue in a distributed, correct, and first-in-first-out (FIFO) manner.

To do this, want to pick the first item in the queue and quickly mark it as selected so that other workers accessing the queue won't process that particular item.

In YugabyteDB, individual statements are transactional, so you just need to select one item, mark it as Running in one statement, and return the selected id. For this, you can use the following:

```plpgsql
UPDATE Jobs SET status = 'Running'
WHERE id = (SELECT id FROM jobqueue FOR UPDATE SKIP LOCKED LIMIT 1)
RETURNING id;
```

This selects the item to be picked, quickly marks it as Running, and returns the item id.

{{<note>}}
During execution, the row is temporarily locked so other workers don't pick it. To avoid contention when other workers try to pick the row, YugabyteDB supports FOR UPDATE SKIP LOCKED. This lets other worker threads quickly skip over the temporarily locked row, instead of being blocked.
{{</note>}}

## Tombstone problem

Over a period of time, the queue could become slower because of ordered deletes of the data. Although YugabyteDB has optimizations to identify this scenario and automatically trigger smart compaction, it is advisable to store the timestamp of the last processed job and give it as a hint to the executor to retrieve the head of the queue.

```sql
select * from jobqueue where create_ts > '2023-07-20 06:41:00' limit 1;
```

{{<tip>}}
To understand the tombstone problem, see [The Curious Case of the Ever-Increasing Tombstones](https://www.yugabyte.com/blog/keep-tombstones-data-markers-slowing-scan-performance/#houston-we-have-a-problem).
{{</tip>}}

## Learn more

[How to Build a Robust Distributed FIFO Job Queue](https://www.yugabyte.com/blog/distributed-fifo-job-queue/)

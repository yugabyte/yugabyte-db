---
title: Query Tuning
headerTitle: Query Tuning
linkTitle: Query tuning
description: Tuning and optimizing query performance
headcontent: Tuning and optimizing query performance
image: /images/section_icons/index/develop.png
menu:
  stable:
    identifier: query-tuning
    parent: explore
    weight: 280
type: indexpage
---

{{<index/block>}}

  {{<index/item
    title="Introduction"
    body="Tune and optimize query performance in YugabyteDB."
    href="query-tuning-intro/"
    icon="/images/section_icons/develop/learn.png">}}

  {{<index/item
    title="Get query statistics using pg_stat_statements"
    body="Track planning and execution statistics for all SQL statements executed by a server."
    href="pg-stat-statements/"
    icon="/images/section_icons/develop/learn.png">}}

  {{<index/item
    title="View live queries with pg_stat_activity"
    body="Troubleshoot problems and identify long-running queries with the activity view."
    href="pg-stat-activity/"
    icon="/images/section_icons/develop/learn.png">}}

  {{<index/item
    title="View terminated queries with yb_terminated_queries"
    body="Identify terminated queries with the get queries function."
    href="yb-pg-stat-get-queries/"
    icon="/images/section_icons/develop/learn.png">}}

  {{<index/item
    title="View COPY status with pg_stat_progress_copy"
    body="Get the COPY command status, number of tuples processed, and other COPY progress reports with this view."
    href="pg-stat-progress-copy/"
    icon="/images/section_icons/develop/learn.png">}}

  {{<index/item
    title="Analyze queries with EXPLAIN"
    body="Tune your queries by creating and analyzing execution plans."
    href="explain-analyze/"
    icon="/images/section_icons/develop/learn.png">}}

  {{<index/item
    title="Optimize YSQL queries using pg_hint_plan"
    body="Control query execution plans with hinting phrases."
    href="pg-hint-plan/"
    icon="/images/section_icons/develop/learn.png">}}

{{</index/block>}}

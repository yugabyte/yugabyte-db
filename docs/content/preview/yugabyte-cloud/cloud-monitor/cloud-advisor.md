---
title: Performance Advisor
linkTitle: Performance Advisor
description: Scan your cluster to dsicover performance optimizations.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  preview:
    identifier: cloud-advisor
    parent: cloud-monitor
    weight: 400
isTocNested: true
showAsideToc: true
---

Use Performance Advisor to scan your cluster for potential optimizations.

For meaningful results, run your workload for at least an hour before running the advisor.

To monitor clusters in real time, use the performance metrics on the cluster [Overview and Performance](../overview/) tabs.

![Performance Advisor](/images/yb-cloud/cloud-alerts-configurations.png)

## Recommendations

Performance Advisor provides recommendations for the following issues.

### Index suggestions

Performance Advisor suggests dropping unused indexes to improve write performance and increase storage space. Performance Advisor flags an index as unused if it hasn't supported a query in 7 or more days after it was created or the server was restarted.

Connect to the database and use DROP INDEX to delete the unused indexes.

LINK: Learn more about index performance tuning

### Schema suggestions

Advisor scans for indexes that can benefit from using range sharding instead of the default hash sharding.

For example, for indexes used on a timestamp column, using range sharding can improve SELECT query performance.

Connect to the database and use DROP INDEX to delete the indexes, and then recreate the indexes using range sharding.

### Connection skew

Advisor scans node connections to determine whether some nodes are loaded with more connections than others. If any node handles 50% more connections than the other nodes in the past hour.

If you are using the YugabyteDB Managed load balancer, open a support ticket.

If you are load balancing in your application or client, review your implementation.

### Query load skew

Advisor scans queries to determine whether some nodes are handling more queries than others. Advisor flags nodes that processed 50% more queries than the other nodes in the past hour.

If you see query load skew, open a support ticket.

### CPU skew

Advisor monitors CPU use to determine whether any nodes become hot spots.

If node CPU use is 50% greater than the other nodes in the cluster in the past hour, or CPU use for a node exceeds 80% for 10 minutes, Advisor flags the issue.

Rebalance and troubleshoot the cluster. Learn How

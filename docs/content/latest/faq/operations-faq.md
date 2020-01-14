---
title: Operations FAQ
linkTitle: Operations FAQ
description: Operations FAQ
menu:
  latest:
    identifier: operations-faq
    parent: faq
    weight: 2740
isTocNested: false
showAsideToc: true
---

## Do YugabyteDB need an external load balancer?

For YSQL, an external load balancer is recommended, but note the following:

- If you use the [YugabyteDB JDBC driver (beta)](../../drivers/yugabytedb-jdbc-driver) with an application regularly opens and closes connections to clients, then the YugabyteDB JDBC driver effectively provides basic load balancing by randomly using connections to your nodes.
- If you use the [Spring Data Yugabyte driver (beta)](../../drivers/yugabytedb-jdbc-driver) with your Spring application, then the underlying YugabyteDB driver provides basic load balancing.
- If you have an application that is not in the same Kubernetes cluster, then you should use an external load balancing system.

For YCQL, YugabyteDB provides automatic load balancing.

## Can write ahead log (WAL) files for YCQL be cleaned up or reduced in size? I'm running out of disk space.

WAL files are per tablet and the retention policy is managed by the following two gflags:

- `src/yb/consensus/log.cc:DEFINE_int32(log_min_segments_to_retain, 2,`
- `src/yb/consensus/log.cc:DEFINE_int32(log_min_seconds_to_retain, 900,`

Also, the following gflag is a factor in the size of each WAL before it is rolled into a new one:

- `src/yb/consensus/log_util.cc:DEFINE_int32(log_segment_size_mb, 64,`

For most YugabyteDB deployments, you shouldn't need to adjust these values. While your data size is small and growing, the WAL files may seem to be much larger, but over time, the WAL files should reach their steady state while the data size continues to grow and become larger than the WAL files.

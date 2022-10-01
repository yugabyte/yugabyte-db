---
title: FAQs about operating YugabyteDB clusters
headerTitle: Operations FAQ
linkTitle: Operations FAQ
description: Answers to common questions about operating YugabyteDB clusters
menu:
  preview_faq:
    identifier: faq-operations
    parent: faq
    weight: 2720
type: docs
---

## Do YugabyteDB clusters need an external load balancer?

For YSQL, an external load balancer is recommended, or you can use a YugabyteDB smart driver. To learn more about smart drivers, refer to [YugabyteDB smart drivers for YSQL](../../drivers-orms/smart-drivers/) and the [Smart driver FAQ](../../drivers-orms/smart-drivers-faq/).

For YCQL, YugabyteDB provides automatic load balancing.

## Can write ahead log (WAL) files be cleaned up or reduced in size?

For most YugabyteDB deployments, you should not need to adjust the configuration flags for the write ahead log (WAL). While your data size is small and growing, the WAL files may seem to be much larger, but over time, the WAL files should reach their steady state while the data size continues to grow and become larger than the WAL files.

WAL files are per tablet and the retention policy is managed by the following two `yb-tserver` configuration flags:

- [`--log_min_segments_to_retain`](../../reference/configuration/yb-tserver/#log-min-segments-to-retain)
- [`--log_min_seconds_to_retain`](../../reference/configuration/yb-tserver/#log-min-seconds-to-retain)

Also, the following `yb-tserver` configuration flag is a factor in the size of each WAL file before it is rolled into a new one:

- [`--log_segment_size_mb`](../../reference/configuration/yb-tserver/#log-segment-size-mb) – default is `64`.

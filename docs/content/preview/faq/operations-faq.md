---
title: FAQs about operating YugabyteDB clusters
headerTitle: Operations FAQ
linkTitle: Operations FAQ
description: Answers to common questions about operating YugabyteDB clusters
menu:
  preview_faq:
    identifier: faq-operations
    parent: faq
    weight: 50
type: docs
unversioned: true
showRightNav: false
---

### Do YugabyteDB clusters need an external load balancer?

For YSQL, you should use a YugabyteDB smart driver. YugabyteDB smart drivers automatically balance connections to the database and eliminate the need for an external load balancer. If you are not using a smart driver, you will need an external load balancer. To learn more about smart drivers, refer to [YugabyteDB smart drivers for YSQL](../../drivers-orms/smart-drivers/).

For YCQL, YugabyteDB provides automatic load balancing.

[YugabyteDB Aeon](../../yugabyte-cloud/) clusters automatically use the uniform load balancing provided by the cloud provider where the cluster is provisioned. YugabyteDB Aeon creates an external load balancer to distribute the connection load across the nodes in a particular region. For multi-region clusters, each region has its own external load balancer. For regular connections, you need to connect to the region of choice, and application connections are then uniformly distributed across the region without the need for any special coding. For more information on connection load balancing in YugabyteDB Aeon, refer to [YugabyteDB smart drivers for YSQL](../../drivers-orms/smart-drivers/#using-smart-drivers-with-yugabytedb-managed).

#### Using GCP load balancers

To configure a YugabyteDB universe deployed on GCP to use GCP-provided load balancers, you must set the [--pgsql_proxy_bind_address 0.0.0.0:5433](../../reference/configuration/yb-tserver/#pgsql-proxy-bind-address) and [--cql_proxy_bind_address 0.0.0.0:9042](../../reference/configuration/yb-tserver/#cql-proxy-bind-address) flags.

To set these flags for a universe in YugabyteDB Anywhere, refer to [Edit configuration flags](../../yugabyte-platform/manage-deployments/edit-config-flags/).

### Can write ahead log (WAL) files be cleaned up or reduced in size?

For most YugabyteDB deployments, you should not need to adjust the configuration flags for the write ahead log (WAL). While your data size is small and growing, the WAL files may seem to be much larger, but over time, the WAL files should reach their steady state while the data size continues to grow and become larger than the WAL files.

WAL files are per tablet and the retention policy is managed by the following two `yb-tserver` configuration flags:

- [`--log_min_segments_to_retain`](../../reference/configuration/yb-tserver/#log-min-segments-to-retain)
- [`--log_min_seconds_to_retain`](../../reference/configuration/yb-tserver/#log-min-seconds-to-retain)

Also, the following `yb-tserver` configuration flag is a factor in the size of each WAL file before it is rolled into a new one:

- [`--log_segment_size_mb`](../../reference/configuration/yb-tserver/#log-segment-size-mb) – default is `64`.

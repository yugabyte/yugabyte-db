---
title: Advanced configurations for CDC using Logical Replication
headerTitle: Advanced configuration
linkTitle: Advanced configuration
description: Advanced Configurations for Logical Replication.
headcontent: Tune your CDC configuration
menu:
  preview:
    parent: explore-change-data-capture-logical-replication
    identifier: advanced-configurations
    weight: 40
type: docs
---

## YB-TServer flags

You can use the following [YB-TServer flags](../../../../reference/configuration/yb-tserver/) to tune logical replication deployment configuration:

- [ysql_yb_default_replica_identity](../../../../reference/configuration/yb-tserver/#ysql-yb-default-replica-identity)
- [cdcsdk_enable_dynamic_table_support](../../../../reference/configuration/yb-tserver/#cdcsdk-enable-dynamic-table-support)
- [cdcsdk_publication_list_refresh_interval_secs](../../../../reference/configuration/yb-tserver/#cdcsdk-publication-list-refresh-interval-secs)
- [cdcsdk_max_consistent_records](../../../../reference/configuration/yb-tserver/#cdcsdk-max-consistent-records)
- [cdcsdk_vwal_getchanges_resp_max_size_bytes](../../../../reference/configuration/yb-tserver/#cdcsdk-vwal-getchanges-resp-max-size-bytes)

## Retention of resources

CDC retains resources (such as WAL segments) that contain information related to the changes involved in the transactions. These resources are typically retained until the consuming client acknowledges the receipt of all the transactions contained in that resource.

Retaining resources has an impact on the system. Clients are expected to consume these transactions within configurable duration limits. Resources will be released if the duration exceeds these configured limits.

Use the following flags to control the duration for which resources are retained:

- [cdc_wal_retention_secs](../../../../reference/configuration/yb-tserver/#cdc-wal-retention-secs)
- [cdc_intent_retention_ms](../../../../reference/configuration/yb-tserver/#cdc-intent-retention-ms)

Resources are retained for each tablet of a table that is part of a database whose changes are being consumed using a replication slot. This includes those tables that may not be currently part of the publication specification.

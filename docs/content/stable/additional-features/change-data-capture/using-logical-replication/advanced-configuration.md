---
title: Advanced configurations for CDC using Logical Replication
headerTitle: Advanced configuration
linkTitle: Advanced configuration
description: Advanced Configurations for Logical Replication.
headcontent: Tune your CDC configuration
aliases:
  - /stable/explore/change-data-capture/using-logical-replication/advanced-configuration/
menu:
  stable:
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

Use the [cdc_intent_retention_ms](../../../../reference/configuration/yb-tserver/#cdc-intent-retention-ms) and [cdc_wal_retention_time_secs](../../../../reference/configuration/yb-tserver/#cdc-wal-retention-time-secs) flags to control the duration for which resources are retained.

Resources are retained for each tablet of a table that is part of a database whose changes are being consumed using a replication slot. This includes those tables that may not be currently part of the publication specification.

Starting from v2024.2.1, the default data retention for CDC is 8 hours, with support for maximum retention up to 24 hours. Prior to v2024.2.1, the default retention for CDC is 4 hours.

{{< warning title="Important" >}}
When using FULL or DEFAULT replica identities, CDC preserves previous row values for UPDATE and DELETE operations. This is done by retaining history for each row in the database through a suspension of the compaction process. Compaction process is halted by setting retention barriers to prevent cleanup of history for those rows that are yet to be streamed to the CDC client. These retention barriers are dynamically managed and advanced only after the CDC events are streamed and explicitly acknowledged by the client, thus allowing compaction of history for streamed rows.

The [cdc_intent_retention_ms](../../../../reference/configuration/yb-tserver/#cdc-intent-retention-ms) flag governs the maximum retention period (default 8 hours). Be aware that any interruption in CDC consumption for extended periods using these replica identities may degrade read performance. This happens because compaction activities are halted in the database when these replica identities are used, leading to inefficient key lookups as reads must traverse multiple SST files.
{{< /warning >}}

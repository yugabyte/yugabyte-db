---
title: Upgrade the YugabyteDB gRPC connector
headerTitle: Upgrade the connector
linkTitle: Upgrade connector
description: Upgrade an existing YugabyteDB gRPC connector deployment to a later version without losing stream position.
headcontent: Move an existing gRPC connector deployment to a later version
menu:
  stable:
    parent: debezium-connector-yugabytedb
    identifier: upgrade-grpc-connector
    weight: 50
type: docs
rightNav:
  hideH4: true
---

The YugabyteDB gRPC connector is based on Debezium and is published as a Kafka Connect plugin at [yugabyte/debezium-connector-yugabytedb GitHub releases](https://github.com/yugabyte/debezium-connector-yugabytedb/releases) {{<icon/github>}}. This page explains how to move an existing gRPC connector deployment to a later version without losing your stream position, and how to handle the cases that need additional steps.

## When to upgrade

Check [yugabyte/debezium-connector-yugabytedb GitHub releases](https://github.com/yugabyte/debezium-connector-yugabytedb/releases) for new versions, and review the release notes as part of planning any YugabyteDB upgrade.

Upgrade the connector:

- _Before upgrading YugabyteDB_ to a release series newer than the one your connector was built for. The connector is backward compatible only; running it against a newer database release is unsupported and can disrupt streaming. See [Choose a connector version](#choose-a-connector-version).
- _When a release fixes a bug or security issue_ that affects your deployment.

As a best practice, run the latest stable connector release regardless of your YugabyteDB version.

## Choose a connector version

For the connector version naming scheme and how connector versions map to YugabyteDB releases, see [Connector compatibility](../debezium-connector-yugabytedb/#connector-compatibility).

Upgrades are forward-only (backward compatible). A newer connector version resumes from checkpoints created by an older version against the same stream ID, so you can upgrade in place. The connector doesn't support downgrade.

Starting with `dz.1.9.5.yb.grpc.2024.2` (YugabyteDB v2024.2), the `transaction.ordering` property is deprecated and will be removed in a future release. For details, see [Transaction ordering](../debezium-connector-yugabytedb/#transaction-ordering).

When you pick a target connector version:

- Validate the target version in a staging environment that mirrors production before you upgrade production.
- Use the _latest stable_ release (recommended). Connectors are backward compatible with YugabyteDB releases, so you don't need to match the connector to the database release.
- Never run a `*.SNAPSHOT.*` build in production; those are pre-releases.
- If no newer release is available, use the last published stable release; don't stay on an older one.
- Read the release notes for the target (and any versions you skip) for breaking changes before you upgrade.

## Standard upgrade in place

Use this path for the common case: a later build with no breaking change that affects you. The stream ID, server-side checkpoints, connector configuration, and Kafka offsets are all preserved.

An in-place upgrade is non-disruptive. The connector records the WAL position (an OpId checkpoint) for the changes it emits. Checkpoints live on the Kafka side (connector offsets) and on the YugabyteDB server (the `cdc_state` table, keyed by stream ID and tablet). On restart, the connector reads the last committed checkpoint and asks the server to resume just after it.

The checkpoint is valid only while the stream ID remains intact. Never delete a stream ID without first deleting all connectors associated with it, or you will lose data. With `snapshot.mode=initial`, if the snapshot for the stream ID is already complete, the connector resumes streaming from the stored checkpoint instead of snapshotting again, so an in-place upgrade does not need a re-snapshot.

Expect a small number of duplicate events around the restart; Debezium events are idempotent.

### Before you begin

- Upgrade the connector first, then YugabyteDB. The connector is backward compatible with earlier database releases but not newer ones, so if a database upgrade is what's driving this, complete the connector upgrade before you upgrade YugabyteDB. See [When to upgrade](#when-to-upgrade).

- In Kafka Connect distributed mode, connector configuration, offsets, and status live in Kafka topics, so the connector recovers cleanly when workers restart. Plan to restart workers one at a time (rolling), and finish with every worker on the same connector version. Don't run mixed versions beyond the rollout window.

### Perform the upgrade

1. Record the current connector version, `database.streamid`, the SMT (transforms) configuration, and the full connector configuration. Confirm the connector is healthy and lag is low.

1. Download the new version of the connector JAR file (`debezium-connector-yugabytedb-<version>.jar`) from [GitHub releases](https://github.com/yugabyte/debezium-connector-yugabytedb/releases) {{<icon/github>}}.

1. Stop the Kafka Connect worker gracefully.

    A graceful shutdown flushes in-flight records to Kafka and commits the last offsets. Because Kafka Connect loads plugins only at worker startup, you must restart the worker to switch connector versions; pausing the connector alone isn't enough.

1. Install the new version.

    In the connector's directory under the Kafka Connect `plugin.path`, delete the old connector JAR file and copy in the one you downloaded, so that only one version is on the plugin path.

1. Restart the Kafka Connect worker.

    It reloads the connector from its stored configuration and resumes from the last committed checkpoint.

1. Confirm the connector is `RUNNING`, there are no errors, and checkpoints and lag are advancing.

With the connector upgraded, you can proceed with the YugabyteDB upgrade if one is planned.

## When a re-snapshot is required

A re-snapshot rebuilds the stream from scratch. Instead of resuming from stored checkpoints, you delete the connector, create a new stream ID, and redeploy the connector so it takes a fresh initial snapshot of the captured tables before it starts streaming.

A re-snapshot is required when the existing stream ID can't be reused:

- The connector was down longer than the CDC retention window, so the server no longer has history to resume from. (Default CDC retention is 8 hours, configurable up to 24 hours on v2024.2.1+; 4 hours before that. See [Retain data for longer durations](../cdc-get-started/#retain-data-for-longer-durations).) The connector reports that it's restarting from a checkpoint YugabyteDB no longer has.
- The stream ID was dropped or expired, or it doesn't include the table you need (for example, a table that had no primary key when the stream was created).
- The target release notes call out a breaking change that isn't compatible with the existing stream.

To re-snapshot:

1. Delete the connector(s) associated with the stream ID.

1. Create a new stream ID. Prefer the PostgreSQL replication-slot interface with the `yb_grpc` plugin (see [Create a gRPC CDC stream](../cdc-get-started/#create-a-grpc-cdc-stream)):

   ```sql
   SELECT * FROM pg_create_logical_replication_slot('my_grpc_slot', 'yb_grpc');
   SELECT yb_stream_id FROM pg_replication_slots WHERE slot_name = 'my_grpc_slot';
   ```

   Alternatively, use [yb-admin](../../../../admin/yb-admin/#create-change-data-stream) (deprecated; use EXPLICIT checkpointing mode):

   ```sh
   yb-admin --master_addresses <master-addresses> \
     create_change_data_stream ysql.<database-name>
   ```

1. Deploy the new connector version with the new `database.streamid`, following the same registration steps as a new deployment (see [Deploy the YugabyteDB gRPC Connector](../cdc-get-started/#deploy-the-yugabytedb-grpc-connector)).

1. In the connector configuration, set `snapshot.mode` to `initial`.

    Because the new stream ID has no completed snapshot, the connector takes a full snapshot of the captured tables and then switches to streaming automatically. For details on snapshot modes, see [Snapshots](../debezium-connector-yugabytedb/#snapshots).

1. Wait for the snapshot to complete and verify that streaming resumes.

    During the re-sync, the sink receives every row again, so downstream consumers reprocess data they have already seen. Make sure they apply events idempotently (for example, upsert on the primary key instead of appending) so the duplicates don't corrupt downstream state.

## Verify the upgrade

After any upgrade, confirm the stream is healthy:

- Connector status: `GET /connectors/<name>/status` returns `RUNNING` for the connector and its tasks, with no failures.
- Checkpoints progressing: Lag drains and the connector keeps committing checkpoints. See [Monitor](../cdc-monitor/) for the YugabyteDB CDC metrics endpoints.
- Events flowing: New changes appear on the expected Kafka topics, transformed by [YBExtractNewRecordState](../yugabytedb-grpc-transformers/#ybextractnewrecordstate) as before.

## Rollback

Downgrade isn't supported. If a new version misbehaves:

- Prefer fixing forward to a later patch on the same line.
- If you must return to a previous version, re-snapshot on that version (new stream ID, fresh snapshot) rather than pointing the previous connector at the existing stream.

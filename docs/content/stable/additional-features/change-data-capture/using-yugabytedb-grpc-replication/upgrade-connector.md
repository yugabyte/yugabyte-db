---
title: Upgrade the YugabyteDB gRPC connector
headerTitle: Upgrade the connector
linkTitle: Upgrade the connector
description: Upgrade an existing YugabyteDB gRPC connector deployment to a newer version without losing stream position.
headcontent: Move an existing gRPC connector deployment to a newer version
menu:
  stable:
    parent: debezium-connector-yugabytedb
    identifier: upgrade-grpc-connector
    weight: 50
type: docs
rightNav:
  hideH4: true
---

The YugabyteDB gRPC connector is based on Debezium and is published as a Kafka Connect plugin at [GitHub releases](https://github.com/yugabyte/debezium-connector-yugabytedb/releases) {{<icon/github>}}. This topic explains how to move an existing gRPC connector deployment to a newer version without losing your stream position, and how to handle the cases that need extra steps.

## Reasons to upgrade

Each connector release ships fixes, features, and YugabyteDB-compatibility changes. Stay current for:

- **Bug fixes** — Correctness and stability fixes such as tablet-split checkpoint resume, before-image handling, and null-key handling.
- **New capabilities** — Recent releases expose `xrepl_origin_id` on change events and improve new-table polling.
- **Performance and security** — Releases tune default polling intervals, optimize the YSQL type registry, and update dependencies that fix CVEs (for example, Jackson and Netty).
- **YugabyteDB compatibility** — Connector releases are cut alongside YugabyteDB releases; newer database behavior needs a recent connector.
- **Supportability** — Support and fixes target current releases. An old build makes triage harder.

## Choose a connector version

Connector versions follow this scheme:

```output
dz.<debezium-base>.yb.grpc.<yugabytedb-series>.<connector-patch>[.SNAPSHOT.<n>]
```

| Part | Example | Meaning |
| :---- | :------ | :------ |
| `dz.<debezium-base>` | `dz.1.9.5` | Upstream Debezium release the connector is built on (the gRPC connector uses Debezium 1.9.5). |
| `yb.grpc` | `yb.grpc` | Identifies the gRPC-protocol connector (distinct from the logical replication connector). |
| `<yugabytedb-series>` | `2025.2` | YugabyteDB release series the build is aligned to. |
| `<connector-patch>` | `.3` | Connector patch in that series. Higher means newer. |
| `.SNAPSHOT.<n>` | `.SNAPSHOT.1` | Pre-release. Don't use in production. |

Release tags carry a leading `v`. For example, version `dz.1.9.5.yb.grpc.2025.2` is tagged `vdz.1.9.5.yb.grpc.2025.2`.

For how connector versions map to YugabyteDB releases, see [Connector compatibility](../debezium-connector-yugabytedb/#connector-compatibility).

When you pick a target version:

- Prefer the **latest stable** release. Upgrades are backward compatible, so the newest stable build is the right target regardless of which YugabyteDB version you run.
- Never run a `*.SNAPSHOT.*` build in production; those are pre-releases.
- If no newer release is available, use the last published stable release; don't stay on an older one.
- Read the release notes for the target (and any versions you skip) for breaking changes before you upgrade.

## Compatibility

Upgrades are forward-only (backward compatible). A newer connector resumes from checkpoints created by an older connector against the same stream ID, so you can upgrade in place. The connector doesn't support downgrade.

Validate the target version in a staging environment that mirrors production before you upgrade production.

Starting with `dz.1.9.5.yb.grpc.2024.2` (YugabyteDB v2024.2), the `transaction.ordering` property is deprecated and will be removed in a future release. If you set it today, plan for its removal. For details, see [Transaction ordering](../debezium-connector-yugabytedb/#transaction-ordering).

## How resume works

The connector records the WAL position (an OpId checkpoint) for the changes it emits. Checkpoints live on the Kafka side (connector offsets) and on the YugabyteDB server (the `cdc_state` table, keyed by stream ID and tablet). On restart, the connector reads the last committed checkpoint and asks the server to resume just after it.

The checkpoint is valid only while the stream ID remains intact. Never delete a stream ID without first deleting all connectors associated with it, or you lose data. With `snapshot.mode=initial`, if the snapshot for the stream ID is already complete, the connector resumes from the stored checkpoint instead of snapshotting again — so an in-place upgrade needs no re-snapshot.

Expect a small number of duplicate events around the restart; Debezium events are idempotent.

## Upgrade in place

Use this path for the common case: a newer build with no breaking change that affects you. The stream ID, server-side checkpoints, connector configuration, and Kafka offsets are all preserved.

1. Before you start, record the current connector version, `database.streamid`, the SMT (transforms) configuration, and the full connector configuration. Confirm the connector is healthy and lag is low.

1. Download the target connector archive from [GitHub releases](https://github.com/yugabyte/debezium-connector-yugabytedb/releases) {{<icon/github>}} and extract it.

1. Stop the Kafka Connect worker gracefully. A graceful shutdown flushes in-flight records to Kafka and commits the last offsets. Plugins load at worker startup, so you need a restart to pick up new JARs.

1. Replace the JARs in the connector's directory under the Kafka Connect `plugin.path`. Remove the old version's JARs and add the new ones. Don't leave both versions on the path.

1. Restart the Kafka Connect worker. It reloads the connector from its stored configuration and resumes from the last committed checkpoint.

1. Confirm the connector is `RUNNING`, there are no errors, and checkpoints and lag are advancing.

After the connector upgrade succeeds, you can upgrade YugabyteDB if needed.

In Kafka Connect distributed mode, configuration, offsets, and status live in Kafka topics, so the connector recovers after a worker restarts. Upgrade workers one at a time (rolling), and get every worker onto the same connector version. Avoid mixed versions beyond the rollout window.

## Perform a re-snapshot

You need a re-snapshot — a new stream ID and a fresh snapshot — when you can't reuse the existing stream ID. For example:

- The connector was down longer than the CDC retention window, so the server no longer has history to resume from. (Default CDC retention is 8 hours, configurable up to 24 hours on v2024.2.1+; 4 hours before that. See [Retain data for longer durations](../cdc-get-started/#retain-data-for-longer-durations).) The connector reports that it's restarting from a checkpoint YugabyteDB no longer has.
- The stream ID was dropped or expired, or it doesn't include the table you need (for example, a table that had no primary key when the stream was created).
- The target release notes call out a breaking change that isn't compatible with the existing stream.

To re-snapshot:

1. Delete the connector(s) associated with the stream ID.

1. Create a new stream ID with [yb-admin](../../../../admin/yb-admin/#create-change-data-stream) (use EXPLICIT checkpointing mode):

   ```sh
   yb-admin --master_addresses <master-addresses> \
     create_change_data_stream ysql.<database-name>
   ```

1. Deploy the new connector version with the new `database.streamid` (and `snapshot.mode=initial`).

1. Wait for the fresh snapshot, then streaming. Expect duplicate events at the sink during re-sync; design consumers to be idempotent.

## Verify the upgrade

After any upgrade, confirm the stream is healthy:

- **Connector status** — `GET /connectors/<name>/status` returns `RUNNING` for the connector and its tasks, with no failures.
- **Checkpoints progressing** — Lag drains and the connector keeps committing checkpoints. See [Monitor](../cdc-monitor/) for the YugabyteDB CDC metrics endpoints.
- **Events flowing** — New changes appear on the expected Kafka topics, transformed by [YBExtractNewRecordState](../yugabytedb-grpc-transformers/#ybextractnewrecordstate) as before.

## Roll back

Downgrade isn't supported. If a new version misbehaves:

- Prefer fixing forward to a newer patch on the same line.
- If you must return to an older version, re-snapshot on that version (new stream ID, fresh snapshot) rather than pointing the old connector at the existing stream.

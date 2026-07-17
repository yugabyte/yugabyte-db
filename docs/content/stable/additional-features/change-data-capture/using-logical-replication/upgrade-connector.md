---
title: Upgrade the YugabyteDB logical replication connector
headerTitle: Upgrade the connector
linkTitle: Upgrade connector
description: Upgrade an existing YugabyteDB logical replication connector deployment to a later version without losing stream position.
headcontent: Move an existing logical replication connector deployment to a later version
menu:
  stable:
    parent: yugabytedb-connector
    identifier: upgrade-logical-connector
    weight: 70
type: docs
rightNav:
  hideH4: true
---

The YugabyteDB connector for logical replication is based on Debezium and is published as a Kafka Connect plugin at [yugabyte/debezium GitHub releases](https://github.com/yugabyte/debezium/releases) {{<icon/github>}}. This page describes how to move an existing connector deployment to a later version without losing your stream position, and how to handle the cases that need additional steps.

## Reasons to upgrade

Each connector release ships fixes, features, and YugabyteDB-compatibility changes. Stay current for:

- Bug fixes. Some defects cause data correctness or streaming issues. For example, version `dz.2.5.2.yb.2025.2` fails to deploy with `cannot export or import snapshot` when `ysql_enable_pg_export_snapshot` is disabled; the fix shipped in `dz.2.5.2.yb.2025.2.2`.
- New capabilities. Recent releases added pgvector support, replication-origin (ORIGIN) messages for bi-directional setups, parallel streaming, transaction savepoints, and upstream heartbeat behavior.
- YugabyteDB compatibility. Connector versions ship alongside YugabyteDB releases; newer database features need a recent connector end to end.
- Supportability. Support and fixes target current releases. An old build makes triage harder.

## Choose a connector version

Connector versions follow this scheme:

```output
dz.<debezium-base>.yb.<yugabytedb-series>.<connector-patch>[.SNAPSHOT.<n>]
```

| Component | Example | Description |
| :---- | :------ | :------ |
| `dz.<debezium-base>` | `dz.2.5.2` | Upstream Debezium release the connector is built on. |
| `yb.<yugabytedb-series>` | `yb.2025.2` | YugabyteDB release series the build is aligned to. |
| `<connector-patch>` | `.3` | Connector patch in that series. Higher is more recent. |
| `.SNAPSHOT.<n>` | `.SNAPSHOT.1` | Pre-release. Don't use in production. |

For how connector versions map to YugabyteDB releases, see [Connector compatibility](../yugabytedb-connector/#connector-compatibility).

When you pick a target Connector version:

- Always use the _latest stable_ release (recommended). Connectors are backward compatible with YugabyteDB releases, so running the latest stable connector is recommended regardless of the version of YugabyteDB you are running. You don't need to match the connector to the database release.
- Never run a `*.SNAPSHOT.*` build in production; those are pre-releases.
- If no newer release is available, use the last published stable release; don't stay on an older one.
- Read the release notes for the target (and any versions you skip) for breaking changes, and confirm the minimum supported YugabyteDB version before you upgrade.

## Compatibility

Upgrades are forward-only (backward compatible). A newer connector version can read the replication slot, publication, and stored offsets created by an older version, so you can upgrade in place and resume streaming. The connector doesn't support downgrade; an older connector version isn't guaranteed to read state that a later one wrote.

Always move to a newer (or the latest stable) version. If you need to go back to a previous version, treat it as a full re-snapshot (see [When a re-snapshot is required](#when-a-re-snapshot-is-required)), not a downgrade.

## Standard upgrade in place

Use this path for the common case: a later build with no breaking change in its release notes. The replication slot, publication, connector configuration, and offsets are all preserved.

### How the connector resumes without a re-snapshot

The connector records the YugabyteDB LSN for every event it emits and externally stores the last processed offset in the Kafka Connect offsets topic. On restart, it asks the server to resume from just after that offset.

The stored offset is valid only while the replication slot remains intact. With the default `snapshot.mode=initial`, the connector takes a snapshot only when no offsets exist, so an in-place upgrade that preserves the slot and offsets resumes streaming with no re-snapshot.

That's why an in-place upgrade is non-disruptive. Expect a small number of duplicate events around the restart; Debezium events are idempotent.

### Perform the upgrade

1. Record the current connector version, `slot.name`, `publication.name`, and the full connector configuration. Confirm the slot is healthy and lag is low.

1. Download the target connector plugin archive from [GitHub releases](https://github.com/yugabyte/debezium/releases) {{<icon/github>}} and extract it.

1. Stop the Kafka Connect worker gracefully. A graceful shutdown flushes in-flight records to Kafka and commits the last offsets. Plugins load at worker startup, so you need a restart to pick up new JARs; pausing the connector alone isn't enough.

1. Replace the JARs in the connector's directory under the Kafka Connect `plugin.path`. Remove the old version's JARs and add the new ones. Don't leave both versions on the path.

1. Restart the Kafka Connect worker. It reloads the connector from its stored configuration and resumes from the last committed offset.

1. Verify that the connector is `RUNNING`, there are no errors, and the slot's restart time and lag are advancing.

After the connector upgrade succeeds, you can upgrade YugabyteDB if needed.

{{< warning title="Replica identity CHANGE and database upgrades" >}}

If any table uses replica identity `CHANGE`, don't upgrade YugabyteDB from a version earlier than v2025.2.3.0 to v2025.2.3.0 or later while the connector is running. A connector that starts or restarts during that upgrade can emit change events with null keys (See {{<issue 32426>}}). Stop the connector before the upgrade and restart it after the upgrade completes on all nodes. Other replica identities are unaffected.

{{< /warning >}}

In Kafka Connect distributed mode, configuration, offsets, and status live in Kafka topics (`config.storage.topic`, `offset.storage.topic`, `status.storage.topic`), so the connector recovers after a worker restarts. Upgrade workers one at a time (rolling), and get every worker onto the same connector version. Avoid mixed versions beyond the rollout window.

## When a re-snapshot is required

You need a re-snapshot: dropping the slot and publication, and streaming again from a fresh initial snapshot when the existing slot cannot be reused. For example:

- The target release notes call out a breaking change that isn't backward compatible with existing slots or offsets.
- The replication slot has expired or become invalid (for example, after certain DDL changes, after point-in-time recovery, or after you add an expired or not-of-interest table to the publication).
- You are planning a YSQL major upgrade (PostgreSQL 11 to PostgreSQL 15); see [Upgrade across a YSQL major version](#ysql-major-upgrade) for the supported flow that avoids a full re-snapshot.

To re-snapshot:

1. Delete the connector.

1. Drop the replication slot and the publication.

1. Deploy the new connector version with a fresh `slot.name` and `publication.name`.

1. With `snapshot.mode=initial`, the connector takes a new initial snapshot and then streams. Expect duplicate events at the sink during re-sync; design consumers to be idempotent.

## Perform a YSQL major upgrade {#ysql-major-upgrade}

Upgrading YugabyteDB from a PostgreSQL 11–based version (v2024.1) to a PostgreSQL 15–based version (v2025.1 or later (stable)) requires special handling for logical replication streams. The database upgrade itself is fully online, but you must pause the stream around finalization. You can upgrade logical replication streams to YugabyteDB v2025.1.1 and later.

For the full procedure, see [YSQL major upgrade - logical replication](../../../../manage/ysql-major-upgrade-logical-replication/).

## Rollback

Downgrade isn't supported. If a new version misbehaves:

- Prefer fixing forward to a later patch on the same line.
- If you must return to a previous version, re-snapshot on that version (new slot, fresh snapshot) rather than pointing the previous connector at the existing slot.

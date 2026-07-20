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

## When to upgrade

Check [yugabyte/debezium GitHub releases](https://github.com/yugabyte/debezium/releases) for new versions, and review the release notes as part of planning any YugabyteDB upgrade.

Upgrade the connector:

- _Before upgrading YugabyteDB_ to a release series newer than the one your connector was built for. The connector is backward compatible only; running it against a newer database release is unsupported and can disrupt streaming. See [Choose a connector version](#choose-a-connector-version).
- _When a release fixes a bug or security issue_ that affects your deployment.

As a best practice, run the latest stable connector release regardless of your YugabyteDB version.

## Choose a connector version

For the connector version naming scheme and how connector versions map to YugabyteDB releases, see [Connector compatibility](../yugabytedb-connector/#connector-compatibility).

Upgrades are forward-only (backward compatible). A newer connector version can read the replication slot, publication, and stored offsets created by an older version, so you can upgrade in place and resume streaming. The connector doesn't support downgrade; an older connector version isn't guaranteed to read state that a later one wrote.

Always move to a newer (or the latest stable) version. If you need to go back to a previous version, treat it as a full re-snapshot (see [When a re-snapshot is required](#when-a-re-snapshot-is-required)), not a downgrade.

When you pick a target connector version:

- Always use the _latest stable_ release (recommended). Connectors are backward compatible with YugabyteDB releases, so you don't need to match the connector to the database release.
- Never run a `*.SNAPSHOT.*` build in production; those are pre-releases.
- If no newer release is available, use the last published stable release; don't stay on an older one.
- Read the release notes for the target (and any versions you skip) for breaking changes, and confirm the minimum supported YugabyteDB version before you upgrade.

## Standard upgrade in place

Use this path for the common case: a later build with no breaking change in its release notes. The replication slot, publication, connector configuration, and offsets are all preserved.

An in-place upgrade is non-disruptive. The connector records the YugabyteDB Log Sequence Number ([LSN](../key-concepts/#lsn-type)) for every event it emits and externally stores the last processed offset in the Kafka Connect offsets topic. On restart, it asks the server to resume from just after that offset.

Expect a small number of duplicate events around the restart; Debezium events are idempotent.

### Before you begin

- Upgrade the connector first, then YugabyteDB. The connector is backward compatible with earlier database releases but not newer ones, so if a database upgrade is what's driving this, complete the connector upgrade before you upgrade YugabyteDB. See [When to upgrade](#when-to-upgrade).

- In Kafka Connect distributed mode, connector configuration, offsets, and status live in Kafka topics (`config.storage.topic`, `offset.storage.topic`, `status.storage.topic`), so the connector recovers cleanly when workers restart. Plan to restart workers one at a time (rolling), and finish with every worker on the same connector version. Don't run mixed versions beyond the rollout window.

{{< warning title="Replica identity CHANGE and database upgrades" >}}

If any table uses replica identity `CHANGE`, don't upgrade YugabyteDB from a version earlier than v2025.2.3.0 to v2025.2.3.0 or later while the connector is running. A connector that starts or restarts during that upgrade can emit change events with null keys (See {{<issue 32426>}}). Stop the connector before the upgrade and restart it after the upgrade completes on all nodes. Other replica identities are unaffected.

{{< /warning >}}

### Perform the upgrade

1. Record the current connector version, `slot.name`, `publication.name`, and the full connector configuration. Confirm the slot is healthy and lag is low.

1. Download the new version of the connector JAR file (`yugabytedb-source-connector-<version>-jar-with-dependencies.jar`) from [GitHub releases](https://github.com/yugabyte/debezium/releases) {{<icon/github>}}.

1. Stop the Kafka Connect worker gracefully. A graceful shutdown flushes in-flight records to Kafka and commits the last offsets. Because Kafka Connect loads plugins only at worker startup, you must restart the worker to switch connector versions; pausing the connector alone isn't enough.

1. Install the new version: in the connector's directory under the Kafka Connect `plugin.path`, delete the old connector JAR file and copy in the one you downloaded, so that only one version is on the plugin path.

1. Restart the Kafka Connect worker. It reloads the connector from its stored configuration and resumes from the last committed offset.

1. Verify that the connector is `RUNNING`, there are no errors, and the slot's restart time and lag are advancing.

With the connector upgraded, you can proceed with the YugabyteDB upgrade if one is planned.

## When a re-snapshot is required

A re-snapshot rebuilds the stream from scratch. Instead of resuming from stored offsets, you delete the connector, drop the replication slot and publication, and redeploy the connector so it takes a fresh initial snapshot of the captured tables before it starts streaming.

A re-snapshot is required when the existing replication slot can't be reused:

- The target release notes call out a breaking change that isn't backward compatible with existing slots or offsets.
- The replication slot has expired or become invalid (for example, after certain DDL changes, after point-in-time recovery, or after you add an expired or not-of-interest table to the publication).
- You are planning a YSQL major upgrade (PostgreSQL 11 to PostgreSQL 15); see [Upgrade across a YSQL major version](#ysql-major-upgrade) for the supported flow that avoids a full re-snapshot.

To re-snapshot:

1. Delete the connector.

1. Drop the replication slot and the publication.

1. Deploy the new connector version with a fresh `slot.name` and `publication.name`, following the same registration steps as a new deployment (see [Deploy the YugabyteDB connector](../get-started/#deploy-the-yugabytedb-connector)).

1. In the connector configuration, leave `snapshot.mode` set to `initial` (the default). Because the new slot has no stored offsets, the connector takes a full snapshot of the captured tables and then switches to streaming automatically.

1. Wait for the snapshot to complete and verify that streaming resumes. During the re-sync, the sink receives every row again, so downstream consumers reprocess data they have already seen. Make sure they apply events idempotently (for example, upsert on the primary key instead of appending) so the duplicates don't corrupt downstream state.

## Perform a YSQL major upgrade {#ysql-major-upgrade}

Upgrading YugabyteDB from a PostgreSQL 11–based version (v2024.1) to a PostgreSQL 15–based version (v2025.1 or later (stable)) requires special handling for logical replication streams. The database upgrade itself is fully online, but you must pause the stream around finalization. You can upgrade logical replication streams to YugabyteDB v2025.1.1 and later.

For the full procedure, see [YSQL major upgrade - logical replication](../../../../manage/ysql-major-upgrade-logical-replication/).

## Rollback

Downgrade isn't supported. If a new version misbehaves:

- Prefer fixing forward to a later patch on the same line.
- If you must return to a previous version, re-snapshot on that version (new slot, fresh snapshot) rather than pointing the previous connector at the existing slot.

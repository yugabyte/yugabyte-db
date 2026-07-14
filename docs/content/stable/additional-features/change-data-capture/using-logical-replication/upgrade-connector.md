---
title: Upgrade the YugabyteDB logical replication connector
headerTitle: Upgrade the connector
linkTitle: Upgrade the connector
description: Upgrade an existing YugabyteDB logical replication connector deployment to a newer version without losing stream position.
headcontent: Move an existing logical replication connector deployment to a newer version
menu:
  stable:
    parent: yugabytedb-connector
    identifier: upgrade-logical-connector
    weight: 70
type: docs
rightNav:
  hideH4: true
---

The YugabyteDB connector for logical replication is based on Debezium and is published as a Kafka Connect plugin at [yugabyte/debezium GitHub releases](https://github.com/yugabyte/debezium/releases) {{<icon/github>}}. This page describes how to move an existing connector deployment to a newer version without losing your stream position, and how to handle the cases that need additional steps.

## Reasons to upgrade

Each connector release ships fixes, features, and YugabyteDB-compatibility changes. Stay current for:

- Bug fixes: Some defects cause data correctness or streaming issues. For example, version `dz.2.5.2.yb.2025.2` fails to deploy with `cannot export or import snapshot` when `ysql_enable_pg_export_snapshot` is disabled; the fix shipped in `dz.2.5.2.yb.2025.2.2`.
- New capabilities: Recent releases added pgvector support, replication-origin (ORIGIN) messages for bi-directional setups, parallel streaming, transaction savepoints, and upstream heartbeat behavior.
- YugabyteDB compatibility: Connector versions ship alongside YugabyteDB releases; newer database features need a recent connector end to end.
- Supportability: Support and fixes target current releases. An old build makes triage harder.

## Choose a connector version

Connector versions follow this scheme:

```output
dz.<debezium-base>.yb.<yugabytedb-series>.<connector-patch>[.SNAPSHOT.<n>]
```

| Part | Example | Meaning |
| :---- | :------ | :------ |
| `dz.<debezium-base>` | `dz.2.5.2` | Upstream Debezium release the connector is built on. |
| `yb.<yugabytedb-series>` | `yb.2025.2` | YugabyteDB release series the build is aligned to. |
| `<connector-patch>` | `.3` | Connector patch in that series. Higher means newer. |
| `.SNAPSHOT.<n>` | `.SNAPSHOT.1` | Pre-release. Don't use in production. |

For how connector versions map to YugabyteDB releases, see [Connector compatibility](../yugabytedb-connector/#connector-compatibility).

When you pick a target version:

- Prefer the _latest stable_ release. Connectors are backward compatible with YugabyteDB releases, so the newest stable build is the right target regardless of which YugabyteDB version you run; you don't need to pin the connector to your database's release series.
- Never run a `*.SNAPSHOT.*` build in production; those are pre-releases.
- If no newer release is available, use the last published stable release; don't stay on an older one.
- Read the release notes for the target (and any versions you skip) for breaking changes, and confirm the minimum supported YugabyteDB version before you upgrade.

## Compatibility

Upgrades are forward-only (backward compatible). A newer connector can read the replication slot, publication, and stored offsets created by an older connector, so you can upgrade in place and resume streaming. The connector doesn't support downgrade; an older connector isn't guaranteed to read state that a newer one wrote.

Always move to a newer (or the latest stable) version. If you need to go back to an older version, treat it as a full re-snapshot (see [Perform a re-snapshot](#perform-a-re-snapshot)), not a downgrade.

## How resume works

The connector records the YugabyteDB LSN for every event it emits and stores the last processed offset in the Kafka Connect offsets topic. On restart it asks the server to resume from just after that offset.

The stored offset is valid only while the replication slot remains intact. With the default `snapshot.mode=initial`, the connector takes a snapshot only when no offsets exist, so an in-place upgrade that preserves the slot and offsets resumes streaming with no re-snapshot.

That's why the standard upgrade below is non-disruptive. Expect a small number of duplicate events around the restart; Debezium events are idempotent.

## Upgrade in place

Use this path for the common case: a newer build with no breaking change in its release notes. The replication slot, publication, connector configuration, and offsets are all preserved.

1. Before you start, record the current connector version, `slot.name`, `publication.name`, and the full connector configuration. Confirm the slot is healthy and lag is low.

1. Download the target connector plugin archive from [GitHub releases](https://github.com/yugabyte/debezium/releases) {{<icon/github>}} and extract it.

1. Stop the Kafka Connect worker gracefully. A graceful shutdown flushes in-flight records to Kafka and commits the last offsets. Plugins load at worker startup, so you need a restart to pick up new JARs; pausing the connector alone isn't enough.

1. Replace the JARs in the connector's directory under the Kafka Connect `plugin.path`. Remove the old version's JARs and add the new ones. Don't leave both versions on the path.

1. Restart the Kafka Connect worker. It reloads the connector from its stored configuration and resumes from the last committed offset.

1. Confirm the connector is `RUNNING`, there are no errors, and the slot's restart time and lag are advancing.

After the connector upgrade succeeds, you can upgrade YugabyteDB if needed.

{{< warning title="Replica identity CHANGE and database upgrades" >}}

If any table uses replica identity `CHANGE`, don't upgrade YugabyteDB from a version earlier than v2025.2.3 to v2025.2.3 or later while the connector is running. A connector that starts or restarts during that upgrade can emit change events with null keys ({{<issue 32426>}}). Stop the connector before the upgrade and restart it after the upgrade completes on all nodes. Other replica identities are unaffected.

{{< /warning >}}

In Kafka Connect distributed mode, configuration, offsets, and status live in Kafka topics (`config.storage.topic`, `offset.storage.topic`, `status.storage.topic`), so the connector recovers after a worker restarts. Upgrade workers one at a time (rolling), and get every worker onto the same connector version. Avoid mixed versions beyond the rollout window.

## Perform a re-snapshot

You need a re-snapshot when you can't reuse the existing slot. To do so, drop the slot and publication, and then stream again from a fresh initial snapshot. For example:

- The target release notes call out a breaking change that isn't backward compatible with existing slots or offsets.
- The replication slot has expired or become invalid (for example, after certain DDL changes, after point-in-time recovery, or after you add an expired or not-of-interest table to the publication).
- You're crossing a YSQL major version (PostgreSQL 11 to PostgreSQL 15); see [Upgrade across a YSQL major version](#ysql-major-upgrade) for the supported flow that avoids a full re-snapshot.

To re-snapshot:

1. Delete the connector.

1. Drop the replication slot and the publication.

1. Deploy the new connector version with a fresh `slot.name` and `publication.name`.

1. With `snapshot.mode=initial`, the connector takes a new initial snapshot and then streams. Expect duplicate events at the sink during re-sync; design consumers to be idempotent.

## Upgrade across a YSQL major version {#ysql-major-upgrade}

Moving YugabyteDB from a PostgreSQL 11–based version to a PostgreSQL 15–based version (v2025.1+ stable) needs special handling for logical replication streams. The database upgrade itself is fully online, but you must pause the stream around finalization. You can upgrade logical replication streams to YugabyteDB v2025.1.1 and later.

For the full procedure, see [YSQL major upgrade - logical replication](../../../../manage/ysql-major-upgrade-logical-replication/).

Don't perform DDL on replicated tables, or modify publications, between `stop_ddl_time` and completing the stream upgrade. Any such DDL renders the slot unusable and forces a new slot (and possibly a fresh snapshot).

### Before you begin

1. Pick a `stop_ddl_time` after which no DDL will run on the replicated tables.

1. Ensure every slot's restart time has crossed `stop_ddl_time`:

   ```plpgsql
   SELECT to_timestamp((yb_restart_commit_ht / 4096) / 1000000)
     AS yb_restart_time FROM pg_replication_slots;
   ```

   ```output
      yb_restart_time
   ------------------------
   2025-09-15 19:58:26+00
   (1 row)
   ```

1. Once `yb_restart_time` for all slots has crossed `stop_ddl_time`, delete the connector and start the database upgrade.

### After you finalize the upgrade

1. Record the time the upgrade finished; call it `upgrade_complete_time`.

1. Redeploy the connector with the existing configuration plus:

   ```yaml
   "ysql.major.upgrade": "true"
   ```

   Without this setting you see:

   ```output
   ERROR:  catalog version for database 16640 was not found.
   HINT:  Database might have been dropped by another user
   STATEMENT:  START_REPLICATION SLOT "test_slot" LOGICAL 0/2D3B0 ("proto_version" '1', "publication_names" 'test_pub', "messages" 'true')
   ```

   Redeploy with this flag before the stream expires — the gap between deleting the connector and redeploying must be shorter than the slot's expiry period. The connector then streams the accumulated lag and advances the slot's restart time.

1. After every slot's restart time has passed `upgrade_complete_time`, restart the connector with:

   ```yaml
   "ysql.major.upgrade": "false"
   ```

   This completes the stream upgrade.

1. Resume DDL and publication changes as needed.

## Verify the upgrade

After any upgrade, confirm the stream is healthy:

- **Connector status** — `GET /connectors/<name>/status` returns `RUNNING` for the connector and its tasks, with no failures.
- **Slot progressing** — `restart_lsn` / `confirmed_flush_lsn` advance and lag drains.

  ```plpgsql
  SELECT slot_name, confirmed_flush_lsn,
         to_timestamp((yb_restart_commit_ht / 4096) / 1000000) AS yb_restart_time
  FROM pg_replication_slots;
  ```

- **Events flowing** — New changes appear on the expected Kafka topics; sink lag returns to normal.

## Roll back

Downgrade isn't supported. If a new version misbehaves:

- Prefer fixing forward to a newer patch on the same line.
- If you must return to an older version, re-snapshot on that version (new slot, fresh snapshot) rather than pointing the old connector at the existing slot.

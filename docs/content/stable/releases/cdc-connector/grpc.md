---
title: gRPC connector releases
headerTitle: CDC gRPC connector
linkTitle: gRPC
description: Release notes and downloads for the YugabyteDB gRPC connector.
headcontent: Release notes and downloads for the YugabyteDB gRPC connector
menu:
  stable_releases:
    identifier: cdc-connector-grpc
    parent: cdc-connector
    weight: 1
    name: gRPC
type: docs
rightNav:
  hideH3: true
  hideH4: true
---

The YugabyteDB gRPC connector is based on Debezium and is published as a Kafka Connect plugin.

[gRPC connector on GitHub](https://github.com/yugabyte/debezium-connector-yugabytedb/) {{<icon/github>}}.

The gRPC connector is the Kafka Connect plugin `debezium-connector-yugabytedb-<version>.jar`. JARs are published to the Yugabyte Maven repository.

For compatibility and configuration, see [YugabyteDB gRPC connector](/stable/additional-features/change-data-capture/using-yugabytedb-grpc-replication/debezium-connector-yugabytedb/).

## dz.1.9.5.yb.grpc.2026.1.2.0.3 - November 5, 2026

**Download**: [GitHub](https://github.com/yugabyte/debezium-connector-yugabytedb/releases/tag/vdz.1.9.5.yb.grpc.2026.1.2.0.3) | [S3 Bucket](https://s3.amazonaws.com/repository.yugabyte.com/maven/release/io/debezium/debezium-connector-yugabytedb/dz.1.9.5.yb.grpc.2026.1.2.0.3/debezium-connector-yugabytedb-dz.1.9.5.yb.grpc.2026.1.2.0.3.jar)

### Supported YugabyteDB releases

This connector release supports all YugabyteDB 2026.1 and earlier releases, as well as all minor versions in the 2026.1.x series. If no connector release is available for your YugabyteDB version, use the latest released connector.

### What's changed

* Add optimizations to connector bootstrap steps. ([#405](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/405))
* Return the cached tablet schema in newTable instead of rebuilding per record. ([#427](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/427))
* DBZ: Stop advancing the snapshot explicit checkpoint past the acknowledged key. {{<issue 35465>}} ([#432](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/432))
* DBZ: Stop one colocated table's acknowledgements from advancing another table's checkpoint. {{<issue 35827>}} {{<issue 35831>}} ([#433](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/433))
* DBZ: Stop advancing the streaming explicit checkpoint past a partly acknowledged transaction. {{<issue 36626>}} ([#434](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/434))
* DBZ: Reported CVEs: upgrade netty, jackson, guava and pgjdbc, move base image to Kafka 4.3.2. #DB-24003 ([#437](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/437))

**Full changelog**: [vdz.1.9.5.yb.grpc.2026.1.2.0.2...vdz.1.9.5.yb.grpc.2026.1.2.0.3](https://github.com/yugabyte/debezium-connector-yugabytedb/compare/vdz.1.9.5.yb.grpc.2026.1.2.0.2...vdz.1.9.5.yb.grpc.2026.1.2.0.3)

## dz.1.9.5.yb.grpc.2026.1.2.0.2 - September 22, 2026

**Download**: [GitHub](https://github.com/yugabyte/debezium-connector-yugabytedb/releases/tag/vdz.1.9.5.yb.grpc.2026.1.2.0.2) | [S3 Bucket](https://s3.amazonaws.com/repository.yugabyte.com/maven/release/io/debezium/debezium-connector-yugabytedb/dz.1.9.5.yb.grpc.2026.1.2.0.2/debezium-connector-yugabytedb-dz.1.9.5.yb.grpc.2026.1.2.0.2.jar)

### Kafka Connect 4.x support

From this release the gRPC connector supports Kafka Connect 4.0 and above.

Earlier releases could not run on a 4.x worker: the jar bundled Kafka's own classes, including its built-in transforms, and those called methods removed in Kafka 4.0. Any connector using a transform such as `ReplaceField` failed at task start with a `NoSuchMethodError`. The jar no longer bundles Kafka classes, so everything resolves against the worker's own version. The bundled JDBC sink was upgraded to `10.6.5-CUSTOM.8` for the same reason.

Kafka Connect 3.9 remains supported and needs no configuration change.

### What's changed

* Stop eagerly evaluating debug/trace log arguments. ([#424](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/424))
* DBZ: Support Kafka Connect 4.x: stop bundling Kafka classes, add ServiceLoader manifests, move base image to Kafka 4.3.1. {{<issue 33702>}} ([#425](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/425))
* DBZ: Do not treat INVALID_REQUEST from GetChanges as a tablet split. {{<issue 33828>}} ([#429](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/429))
* DBZ: Upgrade JDBC sink connector to 10.6.5-CUSTOM.8 for Kafka Connect 4.x. {{<issue 33955>}} ([#430](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/430))

**Full changelog**: [vdz.1.9.5.yb.grpc.2026.1.2.0.1...vdz.1.9.5.yb.grpc.2026.1.2.0.2](https://github.com/yugabyte/debezium-connector-yugabytedb/compare/vdz.1.9.5.yb.grpc.2026.1.2.0.1...vdz.1.9.5.yb.grpc.2026.1.2.0.2)

## dz.1.9.5.yb.grpc.2026.1.2.0.1 - September 7, 2026

**Download**: [GitHub](https://github.com/yugabyte/debezium-connector-yugabytedb/releases/tag/vdz.1.9.5.yb.grpc.2026.1.2.0.1) | [S3 Bucket](https://s3.amazonaws.com/repository.yugabyte.com/maven/release/io/debezium/debezium-connector-yugabytedb/dz.1.9.5.yb.grpc.2026.1.2.0.1/debezium-connector-yugabytedb-dz.1.9.5.yb.grpc.2026.1.2.0.1.jar)

**Full changelog**: [vdz.1.9.5.yb.grpc.2026.1.SNAPSHOT.7...vdz.1.9.5.yb.grpc.2026.1.2.0.1](https://github.com/yugabyte/debezium-connector-yugabytedb/compare/vdz.1.9.5.yb.grpc.2026.1.SNAPSHOT.7...vdz.1.9.5.yb.grpc.2026.1.2.0.1)

### Versioning change

Starting with this release, connector versions follow `dz.<debezium-version>.yb.grpc.<yugabytedb-version>.<connector-patch>`: the YugabyteDB part is now the full four-part release number and the trailing number is the connector patch for that database version. So `dz.1.9.5.yb.grpc.2026.1.2.0.1` is connector patch 1 for YugabyteDB 2026.1.2.0 on Debezium 1.9.5, whereas the previous stable `dz.1.9.5.yb.grpc.2026.1.2` carried only a three-part series with no patch slot.

### What's changed

* DBZ: Use max_index_in_sort_window while sending GetChnages rpcs. {{<issue 30831>}} ([#422](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/422))
* Add connector name tag to JMX metrics. ([#420](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/420))

**Full changelog**: [vdz.1.9.5.yb.grpc.2026.1.2...vdz.1.9.5.yb.grpc.2026.1.2.0.1](https://github.com/yugabyte/debezium-connector-yugabytedb/compare/vdz.1.9.5.yb.grpc.2026.1.2...vdz.1.9.5.yb.grpc.2026.1.2.0.1)

## dz.1.9.5.yb.grpc.2026.1.2 - August 21, 2026

**Download**: [GitHub](https://github.com/yugabyte/debezium-connector-yugabytedb/releases/tag/vdz.1.9.5.yb.grpc.2026.1.2) | [S3 Bucket](https://s3.amazonaws.com/repository.yugabyte.com/maven/release/io/debezium/debezium-connector-yugabytedb/dz.1.9.5.yb.grpc.2026.1.2/debezium-connector-yugabytedb-dz.1.9.5.yb.grpc.2026.1.2.jar)

### Release notes

This release includes 2 major fixes:

#### 1. Data-loss fix

Each record now carries only its own tablet's offset.

Previously, each Kafka record carried offsets for all tablets in the task, which could cause a tablet to commit another tablet's unprocessed offset and lead to silent data loss after a restart. This is rare and needs multiple Kafka brokers to reproduce, since the tablets have to be acked at uneven rates.

#### 2. Heartbeat support

The connector now emits per-tablet heartbeats, enabled via `heartbeat.interval.ms` (default off). Works in both streaming and snapshot.

Some quick points to help with utilisation of this change:

* If you use filtering, enable heartbeats. Filtering worked before only because of the offset bug above, sibling tablets were advancing the filtered tablet's checkpoint. That's gone now, so a filtered tablet will hold its checkpoint and grow WAL forever without a heartbeat.
* Size the interval against your tablet count, since heartbeats are emitted per tablet per interval.

### What's changed

* DBZ: Carry only the record's own tablet checkpoint in its offset. {{<issue 32753>}} ([#413](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/413))
* DBZ: Add per-tablet heartbeat support to the gRPC connector. {{<issue 32752>}} ([#411](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/411))
* Skip record position update on empty transaction COMMIT. ([#419](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/419))

**Full changelog**: [vdz.1.9.5.yb.grpc.2026.1.1...vdz.1.9.5.yb.grpc.2026.1.2](https://github.com/yugabyte/debezium-connector-yugabytedb/compare/vdz.1.9.5.yb.grpc.2026.1.1...vdz.1.9.5.yb.grpc.2026.1.2)

## dz.1.9.5.yb.grpc.2026.1.1 - July 28, 2026

**Download**: [GitHub](https://github.com/yugabyte/debezium-connector-yugabytedb/releases/tag/vdz.1.9.5.yb.grpc.2026.1.1) | [S3 Bucket](https://s3.amazonaws.com/repository.yugabyte.com/maven/release/io/debezium/debezium-connector-yugabytedb/dz.1.9.5.yb.grpc.2026.1.1/debezium-connector-yugabytedb-dz.1.9.5.yb.grpc.2026.1.1.jar)

### What's changed

* DBZ: Restore deprecated cdc.poll.interval.ms as an override for active/idle poll intervals. ([#410](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/410))
* DBZ CDCSDK: Support the yb_grpc replication slot plugin. {{<issue 32629>}} ([#409](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/409))
* DBZ CDCSDK: Removing check of using slot name in config if available for a stream. {{<issue 32888>}} ([#414](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/414))

### Upgrade recommendation

* It is recommended to use this connector version, or any later version, with YugabyteDB versions **2026.1.1.0 and later**.
* Note: This connector is backward compatible with any previous YugabyteDB version.

**Full changelog**: [vdz.1.9.5.yb.grpc.2026.1...vdz.1.9.5.yb.grpc.2026.1.1](https://github.com/yugabyte/debezium-connector-yugabytedb/compare/vdz.1.9.5.yb.grpc.2026.1...vdz.1.9.5.yb.grpc.2026.1.1)

## dz.1.9.5.yb.grpc.2026.1 - June 30, 2026

**Download**: [GitHub](https://github.com/yugabyte/debezium-connector-yugabytedb/releases/tag/vdz.1.9.5.yb.grpc.2026.1) | [S3 Bucket](https://s3.amazonaws.com/repository.yugabyte.com/maven/release/io/debezium/debezium-connector-yugabytedb/dz.1.9.5.yb.grpc.2026.1/debezium-connector-yugabytedb-dz.1.9.5.yb.grpc.2026.1.jar)

### What's changed

* Log tablet and opid got from Kafka callback at info level. {{<issue 30366>}} ([#398](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/398))
* DBZ: Add unit tests for tablet split partial polling and checkpoint resume. {{<issue 30210>}} ([#403](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/403))
* Expose xrepl_origin_id on DML change events. ([#400](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/400))
* Handle NPE in isBeforeImageEnabled when using Logical Replication. {{<issue 30391>}} ([#402](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/402))
* DBZ: Upgrade dependencies. {{<issue 31129>}} ([#407](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/407))
* Add validation for mutually exclusive Stream ID and Slot Name. {{<issue 31778>}} ([#404](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/404))
* DBZ: Followup for Upgrade dependencies changes. {{<issue 31129>}} ([#408](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/408))

### Upgrade recommendation

* This connector version, and all earlier connector versions, are **not compatible** with YugabyteDB **2026.1.1.0 and later**. Please upgrade to **vdz.1.9.5.yb.grpc.2026.1.1** or a later connector version.

**Full changelog**: [vdz.1.9.5.yb.grpc.2025.2...vdz.1.9.5.yb.grpc.2026.1](https://github.com/yugabyte/debezium-connector-yugabytedb/compare/vdz.1.9.5.yb.grpc.2025.2...vdz.1.9.5.yb.grpc.2026.1)

## dz.1.9.5.yb.grpc.2025.2 - February 12, 2026

**Download**: [GitHub](https://github.com/yugabyte/debezium-connector-yugabytedb/releases/tag/vdz.1.9.5.yb.grpc.2025.2) | [S3 Bucket](https://s3.amazonaws.com/repository.yugabyte.com/maven/release/io/debezium/debezium-connector-yugabytedb/dz.1.9.5.yb.grpc.2025.2/debezium-connector-yugabytedb-dz.1.9.5.yb.grpc.2025.2.jar)

### What's changed

* Version change from 2024.2 to 2025.1. ([#393](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/393))
* DBZ: Update the default value of cdc.poll.interval.ms. {{<issue 29850>}} ([#392](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/392))
* Fix: build YSQL type registry with two-pass in-memory prime to avoid N+1 lookups. ([#396](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/396))
* DBZ: Upgrade Jackson Version in gRPC Connector. {{<issue 29356>}} ([#397](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/397))

**Full changelog**: [vdz.1.9.5.yb.grpc.2025.1...vdz.1.9.5.yb.grpc.2025.2](https://github.com/yugabyte/debezium-connector-yugabytedb/compare/vdz.1.9.5.yb.grpc.2025.1...vdz.1.9.5.yb.grpc.2025.2)

## dz.1.9.5.yb.grpc.2025.1 - December 8, 2025

**Download**: [GitHub](https://github.com/yugabyte/debezium-connector-yugabytedb/releases/tag/vdz.1.9.5.yb.grpc.2025.1) | [S3 Bucket](https://s3.amazonaws.com/repository.yugabyte.com/maven/release/io/debezium/debezium-connector-yugabytedb/dz.1.9.5.yb.grpc.2025.1/debezium-connector-yugabytedb-dz.1.9.5.yb.grpc.2025.1.jar)

### What's changed

* Update protobuf version to 4.30.2. ([#375](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/375))
* Run YCQL CDC tests with explicit checkpoint. ([#377](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/377))
* DBZ: Update push GitHub action. ([#380](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/380))
* DBZ: Handle Ysql tables with space in the name. {{<issue 29349>}} ([#386](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/386))
* DBZ: Table filtering in YugabyteDBTablePoller by including database name in TableId catalog. {{<issue 29349>}} ([#388](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/388))

**Full changelog**: [vdz.1.9.5.yb.grpc.2024.2.3...vdz.1.9.5.yb.grpc.2025.1](https://github.com/yugabyte/debezium-connector-yugabytedb/compare/vdz.1.9.5.yb.grpc.2024.2.3...vdz.1.9.5.yb.grpc.2025.1)

## dz.1.9.5.yb.grpc.2024.2.3 - June 13, 2025

**Download**: [GitHub](https://github.com/yugabyte/debezium-connector-yugabytedb/releases/tag/vdz.1.9.5.yb.grpc.2024.2.3) | [S3 Bucket](https://s3.amazonaws.com/repository.yugabyte.com/maven/release/io/debezium/debezium-connector-yugabytedb/dz.1.9.5.yb.grpc.2024.2.3/debezium-connector-yugabytedb-dz.1.9.5.yb.grpc.2024.2.3.jar)

### What's changed

* Fix README.md with correct connector class name and image versions. ([#365](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/365))
* DBZ: For unit test runs. ([#370](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/370))
* DBZ: Test infra failure caused by #370. ([#371](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/371))
* DBZ: Do not extract table name from transactional records. {{<issue 27248>}} ([#372](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/372))
* Modify YBExtractNewRecordState to introduce property "delete.to.tombstone". {{<issue 27255>}} ([#367](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/367))
* DBZ: Improve table filtering logic in consistent streaming source. ([#374](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/374))

**Full changelog**: [vdz.1.9.5.yb.grpc.2024.2.2...vdz.1.9.5.yb.grpc.2024.2.3](https://github.com/yugabyte/debezium-connector-yugabytedb/compare/vdz.1.9.5.yb.grpc.2024.2.2...vdz.1.9.5.yb.grpc.2024.2.3)

## dz.1.9.5.yb.grpc.2024.2.2 - February 7, 2025

**Download**: [GitHub](https://github.com/yugabyte/debezium-connector-yugabytedb/releases/tag/vdz.1.9.5.yb.grpc.2024.2.2) | [S3 Bucket](https://s3.amazonaws.com/repository.yugabyte.com/maven/release/io/debezium/debezium-connector-yugabytedb/dz.1.9.5.yb.grpc.2024.2.2/debezium-connector-yugabytedb-dz.1.9.5.yb.grpc.2024.2.2.jar)

### What's changed

* Fix issue with handling of null values for money type. {{<issue 25143>}} ([#361](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/361))
* Fix value resolver for money type columns. {{<issue 25143>}} ([#364](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/364))
* CDC: Fix vulnerabilities by upgrading dependency versions. ([#363](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/363))

**Full changelog**: [vdz.1.9.5.yb.grpc.2024.2...vdz.1.9.5.yb.grpc.2024.2.2](https://github.com/yugabyte/debezium-connector-yugabytedb/compare/vdz.1.9.5.yb.grpc.2024.2...vdz.1.9.5.yb.grpc.2024.2.2)

## dz.1.9.5.yb.grpc.2024.2 - November 27, 2024

**Download**: [GitHub](https://github.com/yugabyte/debezium-connector-yugabytedb/releases/tag/vdz.1.9.5.yb.grpc.2024.2) | [S3 Bucket](https://s3.amazonaws.com/repository.yugabyte.com/maven/release/io/debezium/debezium-connector-yugabytedb/dz.1.9.5.yb.grpc.2024.2/debezium-connector-yugabytedb-dz.1.9.5.yb.grpc.2024.2.jar)

### Summary

#### Deprecation of transaction ordering

Starting this release, the configuration property `transaction.ordering` has been deprecated and cannot be used. This feature is offered by the logical replication model by default and users are recommended to use the same to leverage transaction ordering.

For cases where using transactional ordering is absolutely necessary with the gRPC connector for now, add the following configurations properties:

```json
"transaction.ordering":"true",
"TEST.override.transaction.ordering.deprecation":"true"
```

#### Disabling connector deployment with IMPLICIT stream ID

This release also disabled a connector deployment when the DB stream ID is in `IMPLICIT` checkpointing mode. This is done to make a complete transition to `EXPLICIT` checkpointing mode as the support for `IMPLICIT` streams will be removed in future releases.

For cases where you absolutely need to use `IMPLICIT` streams, add the following configuration to your connector:

```json
"force.use.implicit.stream":"true"
```

### What's changed

* Updated jenkins timeout. ([#355](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/355))
* DBZ: Disable IMPLICIT checkpointing support in connector. ([#356](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/356))
* DBZ: Enhance GitHub actions for sanity checks. ([#354](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/354))
* DBZ CDCSDK: Fixed vulnerabilities by upgrading dependency versions. ([#358](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/358))
* DBZ CDCSDK: Disable transaction ordering with gRPC connector. {{<issue 23903>}} ([#359](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/359))
* Fix for bug while merging offset result calculations. {{<issue 24992>}} ([#360](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/360))

**Full changelog**: [vdz.1.9.5.yb.grpc.2024.1...vdz.1.9.5.yb.grpc.2024.2](https://github.com/yugabyte/debezium-connector-yugabytedb/compare/vdz.1.9.5.yb.grpc.2024.1...vdz.1.9.5.yb.grpc.2024.2)

## dz.1.9.5.yb.grpc.2024.1 - September 5, 2024

**Download**: [GitHub](https://github.com/yugabyte/debezium-connector-yugabytedb/releases/tag/vdz.1.9.5.yb.grpc.2024.1) | [S3 Bucket](https://s3.amazonaws.com/repository.yugabyte.com/maven/release/io/debezium/debezium-connector-yugabytedb/dz.1.9.5.yb.grpc.2024.1/debezium-connector-yugabytedb-dz.1.9.5.yb.grpc.2024.1.jar)

### Summary

#### Name change

YugabyteDB now has two replication models to offer i.e. logical replication and gRPC replication, see [Change data capture](/stable/additional-features/change-data-capture/) for more.

To keep naming consistent with the model, the connector's name has been changed to `YugabyteDBgRPCConnector` with full class name now being `io.debezium.connector.yugabytedb.YugabyteDBgRPCConnector`. New deployments will need to be created using this new name.

##### Upgrade

For users who are upgrading from a previous version to this version, they will need to edit their configuration file with the new connector class name. The following steps need to be followed:

1. Delete the existing connector from the environment.
2. Place the new connector jar file in the plugins directory.
3. Deploy a new connector with `connector.class=io.debezium.connector.yugabytedb.YugabyteDBgRPCConnector` - rest other configuration properties need not be changed.

#### New versioning scheme

With this release, the connector is moving to a new versioning scheme which is more clear about the base Debezium versions as well as the supported DB version. The new version will be of the format `dz.<debeziumBaselineVersion>.yb.grpc.<highestDBVersionSupported>`. The current version. `dz.1.9.5.yb.grpc.2024.1` can be interpreted as:

* `dz.1.9.5` - Baseline Debezium core version being used is `1.9.5.Final`.
* `yb.grpc.2024.1` - This connectors supports the YugabyteDB versions 2024.1 and lower.

### What's changed

* DBZ: Change versioning scheme for connector. ([#339](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/339))
* DBZ: Renamed main class according to the new convention. ([#341](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/341))
* DBZ: Implement OffsetContext.Loader class properly. ([#347](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/347))
* Revert "DBZ Changed log level for records to `TRACE`". ([#350](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/350))
* DBZ: Read offset from Kafka for every commit callback. ([#348](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/348))
* DBZ: Read offsets from Kafka with tablet splitting support. ([#351](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/351))
* DBZ: Keep explicit checkpoint lower than or equal to the request OpId. ([#352](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/352))
* DBZ: Bug fix for snapshot not transitioning to streaming. ([#353](https://github.com/yugabyte/debezium-connector-yugabytedb/pull/353))

**Full changelog**: [v1.9.5.y.220.4...vdz.1.9.5.yb.grpc.2024.1](https://github.com/yugabyte/debezium-connector-yugabytedb/compare/v1.9.5.y.220.4...vdz.1.9.5.yb.grpc.2024.1)

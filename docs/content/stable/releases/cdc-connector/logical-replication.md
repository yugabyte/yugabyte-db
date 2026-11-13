---
title: Logical replication connector releases
headerTitle: CDC Logical Replication connector
linkTitle: Logical replication
description: Release notes and downloads for the YugabyteDB logical replication connector.
headcontent: Release notes and downloads for the YugabyteDB logical replication connector
menu:
  stable_releases:
    identifier: cdc-connector-logical
    parent: cdc-connector
    weight: 2
    name: Logical replication
type: docs
rightNav:
  hideH3: true
  hideH4: true
---

The YugabyteDB connector for logical replication is based on Debezium and is published as a Kafka Connect plugin.

[Logical replication connector on GitHub](https://github.com/yugabyte/debezium/) {{<icon/github>}}.

For compatibility and configuration, see [YugabyteDB connector](/stable/additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/).

## dz.2.5.2.yb.2026.1.2.0.1 - September 30, 2026

**Download**: [GitHub](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2026.1.2.0.1)

### Versioning change

Starting with this release, connector versions follow `dz.<debezium-version>.yb.<yugabytedb-version>.<connector-patch>`: the YugabyteDB part is now the full four-part release number and the trailing number is the connector patch for that database version. So `dz.2.5.2.yb.2026.1.2.0.1` is connector patch 1 for YugabyteDB 2026.1.2.0 on Debezium 2.5.2, whereas the previous stable `dz.2.5.2.yb.2026.1` carried only the release series, with an optional connector patch.

### What's changed

* DBZ-PGYB: Derive replication slot flush LSN from committed offsets instead of commitRecord callbacks. {{<issue 33126>}} ([#202](https://github.com/yugabyte/debezium/pull/202))
* DBZ-PGYB: Add rate-limited INFO log for emitted heartbeat records. {{<issue 33810>}} ([#203](https://github.com/yugabyte/debezium/pull/203))
* DBZ-PGYB: Upgrade driver to 42.7.3-yb-5 and make the keepalive LSN flush configurable via lsn.flush.mode. {{<issue 33418>}} ([#204](https://github.com/yugabyte/debezium/pull/204))

**Full changelog**: [dz.2.5.2.yb.2026.1...dz.2.5.2.yb.2026.1.2.0.1](https://github.com/yugabyte/debezium/compare/dz.2.5.2.yb.2026.1...dz.2.5.2.yb.2026.1.2.0.1)

## dz.2.5.2.yb.2026.1 - July 7, 2026

**Download**: [GitHub](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2026.1)

### What's changed

* DBZ-PGYB: Fix null Kafka key when connector resumes after table drop. {{<issue 30733>}} ([#195](https://github.com/yugabyte/debezium/pull/195))
* DBZ-PGYB: Selectively call ALTER PUBLICATION for filtered publication. {{<issue 30977>}} ([#196](https://github.com/yugabyte/debezium/pull/196))
* DBZ-PGYB: Filter UPDATE/DELETE on non-PK tables with non-FULL replica identity. {{<issue 30885>}} ([#197](https://github.com/yugabyte/debezium/pull/197))
* DBZ-PGYB: Remove out of band query. {{<issue 30733>}} ([#199](https://github.com/yugabyte/debezium/pull/199))
* DBZ-PGYB: Reject unsupported slot.seek.to.known.offset.on.start property. {{<issue 32007>}} ([#200](https://github.com/yugabyte/debezium/pull/200))
* DBZ-PGYB: Support upstream heartbeat behavior during streaming phase. {{<issue 31289>}} ([#198](https://github.com/yugabyte/debezium/pull/198))
* DBZ-PGYB: Version-gate primary-key resolution for CHANGE replica identity. {{<issue 22555>}} ([#201](https://github.com/yugabyte/debezium/pull/201))

**Full changelog**: [dz.2.5.2.yb.2025.2.3...dz.2.5.2.yb.2026.1](https://github.com/yugabyte/debezium/compare/dz.2.5.2.yb.2025.2.3...dz.2.5.2.yb.2026.1)

## dz.2.5.2.yb.2025.2.3 - February 24, 2026

**Download**: [GitHub](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2025.2.3)

### What's changed

* DBZ-PGYB: Return dummy value from currentXLogLocation() for YugabyteDB. {{<issue 30378>}} ([#193](https://github.com/yugabyte/debezium/pull/193))

**Full changelog**: [dz.2.5.2.yb.2025.2.2...dz.2.5.2.yb.2025.2.3](https://github.com/yugabyte/debezium/compare/dz.2.5.2.yb.2025.2.2...dz.2.5.2.yb.2025.2.3)

## dz.2.5.2.yb.2025.2.2 - February 12, 2026

**Download**: [GitHub](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2025.2.2)

### What's changed

* DBZ-PGYB: Fix fallback when export_snapshot is disabled. {{<issue 30256>}} ([#192](https://github.com/yugabyte/debezium/pull/192))

**Full changelog**: [dz.2.5.2.yb.2025.2...dz.2.5.2.yb.2025.2.2](https://github.com/yugabyte/debezium/compare/dz.2.5.2.yb.2025.2...dz.2.5.2.yb.2025.2.2)

## dz.2.5.2.yb.2025.2 - February 1, 2026

**Download**: [GitHub](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2025.2)

### What's changed

* Add support for publishing the connector jar to YB s3 maven. {{<issue 29499>}} ([#187](https://github.com/yugabyte/debezium/pull/187))
* DBZ-PGYB: Upgrade Java version to Java 17 in YB connector. {{<issue 29660>}} ([#190](https://github.com/yugabyte/debezium/pull/190))
* DBZ-PGYB: Add Pgvector support. {{<issue 29599>}} ([#189](https://github.com/yugabyte/debezium/pull/189))
* DBZ-PGYB: Change default from USE_SNAPSHOT TO EXPORT_SNAPSHOT. {{<issue 27253>}} ([#188](https://github.com/yugabyte/debezium/pull/188))
* DBZ-PGYB: Add support for ORIGIN Message in Yugabyte Connector. {{<issue 29640>}} ([#191](https://github.com/yugabyte/debezium/pull/191))

**Full changelog**: [dz.2.5.2.yb.2025.1.2...dz.2.5.2.yb.2025.2](https://github.com/yugabyte/debezium/compare/dz.2.5.2.yb.2025.1.2...dz.2.5.2.yb.2025.2)

## dz.2.5.2.yb.2025.1.2 - November 12, 2025

**Download**: [GitHub](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2025.1.2) | [S3 Bucket](https://s3.amazonaws.com/repository.yugabyte.com/maven/release/io/debezium/yugabytedb-source-connector/dz.2.5.2.yb.2025.1.2/yugabytedb-source-connector-dz.2.5.2.yb.2025.1.2.jar)

### What's changed

* FIx: Upgrade jackson version to 2.19.0 to fix CVE-2025-52999. ([#186](https://github.com/yugabyte/debezium/pull/186))

**Full changelog**: [dz.2.5.2.yb.2025.1...dz.2.5.2.yb.2025.1.2](https://github.com/yugabyte/debezium/compare/dz.2.5.2.yb.2025.1...dz.2.5.2.yb.2025.1.2)

## dz.2.5.2.yb.2025.1 - October 15, 2025

**Download**: [GitHub](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2025.1)

### What's changed

* Introduce YSQL major upgrade config property. {{<issue 27177>}} ([#185](https://github.com/yugabyte/debezium/pull/185))
* Make yb.load.balance.connections connector config a string instead of boolean. {{<issue 28533>}} ([#184](https://github.com/yugabyte/debezium/pull/184))

**Full changelog**: [dz.2.5.2.yb.2024.2.5...dz.2.5.2.yb.2025.1](https://github.com/yugabyte/debezium/compare/dz.2.5.2.yb.2024.2.5...dz.2.5.2.yb.2025.1)

## dz.2.5.2.yb.2024.2.5 - July 31, 2025

**Download**: [GitHub](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2024.2.5)

### What's changed

* fix: upgrade protobuf-jar version to fix reported CVE-2024-7254. ([#180](https://github.com/yugabyte/debezium/pull/180))
* misc: Rename artifact ID for the YugabyteDB connector. ([#181](https://github.com/yugabyte/debezium/pull/181))

**Full changelog**: [dz.2.5.2.yb.2024.2.4...dz.2.5.2.yb.2024.2.5](https://github.com/yugabyte/debezium/compare/dz.2.5.2.yb.2024.2.4...dz.2.5.2.yb.2024.2.5)

## dz.2.5.2.yb.2024.2.4 - June 13, 2025

**Download**: [GitHub](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2024.2.4)

### What's changed

* DBZ-PGYB: Increase test coverage for partitioned tables. ([#176](https://github.com/yugabyte/debezium/pull/176))
* DBZ-PGYB: Do not set GUC to disable catalog version check with every connection. ([#179](https://github.com/yugabyte/debezium/pull/179))

**Full changelog**: [dz.2.5.2.yb.2024.2.3...dz.2.5.2.yb.2024.2.4](https://github.com/yugabyte/debezium/compare/dz.2.5.2.yb.2024.2.3...dz.2.5.2.yb.2024.2.4)

## dz.2.5.2.yb.2024.2.3 - March 18, 2025

**Download**: [GitHub](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2024.2.3)

### What's changed

* DBZ-PGYB: Changes to support LSN types with replication slot. {{<issue 24204>}} ([#162](https://github.com/yugabyte/debezium/pull/162))
* Make load-balance property configurable. {{<issue 26004>}} ([#171](https://github.com/yugabyte/debezium/pull/171))
* Custom metrics implementation for multi task model. {{<issue 26069>}} ([#165](https://github.com/yugabyte/debezium/pull/165))
* Parellel streaming changes. {{<issue 26107>}} ([#172](https://github.com/yugabyte/debezium/pull/172))
* CDC: Fix validation logic for configs related to parallel streaming. ([#173](https://github.com/yugabyte/debezium/pull/173))
* DBZ-PGYB: Bug fix for inability to alter publication upon transitioning to streaming. ([#174](https://github.com/yugabyte/debezium/pull/174))
* DBZ-PGYB: Specify only HYBRID_TIME when creating slot, skip otherwise. ([#175](https://github.com/yugabyte/debezium/pull/175))

**Full changelog**: [dz.2.5.2.yb.2024.2.2...dz.2.5.2.yb.2024.2.3](https://github.com/yugabyte/debezium/compare/dz.2.5.2.yb.2024.2.2...dz.2.5.2.yb.2024.2.3)

## dz.2.5.2.yb.2024.2.2 - February 4, 2025

**Download**: [GitHub](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2024.2.2)

### What's changed

* DBZ-PGYB: Add transformer `PGCompatible` to emit events with standard structure. ([#168](https://github.com/yugabyte/debezium/pull/168))
* DBZ-PGYB: Bug fix to use correct getter for maximum retries. ([#170](https://github.com/yugabyte/debezium/pull/170))
* DBZ-PGYB: Set GUC to disable catalog version check for connections. {{<issue 25716>}} ([#169](https://github.com/yugabyte/debezium/pull/169))

**Full changelog**: [dz.2.5.2.yb.2024.2...dz.2.5.2.yb.2024.2.2](https://github.com/yugabyte/debezium/compare/dz.2.5.2.yb.2024.2...dz.2.5.2.yb.2024.2.2)

## dz.2.5.2.yb.2024.2 - December 26, 2024

**Download**: [GitHub](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2024.2)

### What's changed

* DBZ-PGYB: Add plugin to package connector in a ZIP file. ([#157](https://github.com/yugabyte/debezium/pull/157))
* DBZ-PGYB: Execute snapshot in chunks. {{<issue 24200>}} ([#161](https://github.com/yugabyte/debezium/pull/161))
* DBZ-PGYB: Add task ID to PostgresPartition. {{<issue 24555>}} ([#163](https://github.com/yugabyte/debezium/pull/163))
* DBZ-PGYB: Set transaction isolation level before executing snapshot query. ([#164](https://github.com/yugabyte/debezium/pull/164))
* DBZ-PGYB: Restrict retries when retry count reaches limit. ([#166](https://github.com/yugabyte/debezium/pull/166))
* DBZ-PGYB: Bug fix for retry limit and test addition. ([#167](https://github.com/yugabyte/debezium/pull/167))

**Full changelog**: [dz.2.5.2.yb.2024.1...dz.2.5.2.yb.2024.2](https://github.com/yugabyte/debezium/compare/dz.2.5.2.yb.2024.1...dz.2.5.2.yb.2024.2)

## dz.2.5.2.yb.2024.1 - August 8, 2024

**Download**: [GitHub](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2024.1)

### What's changed

* Fix log statement. ([#138](https://github.com/yugabyte/debezium/pull/138))
* Fix log statement wihile skipping empty transactions. ([#139](https://github.com/yugabyte/debezium/pull/139))
* DBZ-PGYB: Change default plugin to yboutput. {{<issue 23153>}} ([#140](https://github.com/yugabyte/debezium/pull/140))
* DBZ-PGYB: Change versioning schema to follow YB standards. {{<issue 23081>}} ([#136](https://github.com/yugabyte/debezium/pull/136))
* DBZ-PGYB: Renamed connector to new naming convention. {{<issue 23081>}} ([#142](https://github.com/yugabyte/debezium/pull/142))
* DBZ-PGYB: Remove dependency on custom debezium-core changes. {{<issue 23082>}} ([#137](https://github.com/yugabyte/debezium/pull/137))
* DBZ-PGYB: Fixed flakiness for the tests in PostgresConnectorIT. ([#143](https://github.com/yugabyte/debezium/pull/143))
* DBZ-PGYB: Add new test class `YBRecordsStreamProducerIT` to add tests for datatypes. ([#144](https://github.com/yugabyte/debezium/pull/144))
* DBZ-PGYB: Enable more test classes in the PG connector test suite. ([#141](https://github.com/yugabyte/debezium/pull/141))
* DBZ-PGYB: Rename jar artefact according to new naming standards. ([#146](https://github.com/yugabyte/debezium/pull/146))
* DBZ-PGYB: Non-key columns should always be optional for replica identity CHANGE. ([#147](https://github.com/yugabyte/debezium/pull/147))
* DBZ-PGYB: Fix vulnerability in the Postgres JDBC driver. ([#148](https://github.com/yugabyte/debezium/pull/148))
* DBZ-PGYB: GitHub action pipeline to publish to Quay. ([#145](https://github.com/yugabyte/debezium/pull/145))
* DBZ-PGYB: Dockerfile fix to package proper artefact. ([#149](https://github.com/yugabyte/debezium/pull/149))
* DBZ-PGYB: Add tests to enhance test suite for YugabyteDBConnector. ([#150](https://github.com/yugabyte/debezium/pull/150))
* DBZ-PGYB: Fixed bug resulting a `0` value in case of `pgoutput`. ([#151](https://github.com/yugabyte/debezium/pull/151))
* DBZ-PGYB: Decimal handling mode precise should throw exception. ([#155](https://github.com/yugabyte/debezium/pull/155))
* DBZ-PGYB: Test addition for updating columns of all data types. ([#154](https://github.com/yugabyte/debezium/pull/154))
* DBZ-PGYB: Modify GitHub Action to publish to set proper versions. ([#156](https://github.com/yugabyte/debezium/pull/156))

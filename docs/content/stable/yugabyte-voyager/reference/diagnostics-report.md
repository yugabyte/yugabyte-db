---
title: YB Voyager diagnostics reporting
linkTitle: Diagnostics reporting
description: Sending diagnostics information to Yugabyte.
menu:
  stable_yugabyte-voyager:
    identifier: diagnostics-report
    parent: reference-voyager
    weight: 105
aliases:
  - /stable/yugabyte-voyager/diagnostics-report/
type: docs
---

By default, YugabyteDB Voyager reports migration diagnostics to the Yugabyte diagnostics service when yb-voyager commands are executed. The diagnostics information is transmitted securely to and stored by Yugabyte.

None of your data or personally identifiable information is collected.

## Data collected

When enabled, the following data is collected while running yb-voyager:

- Migration unique ID
- Migration phase name (for example, export-data, import-schema, and so on)
- Migration phase start time
- Migration phase payload (information relevant to that phase of the migration)
- Payload collection time
- Migration type (Offline, live migration (with fall-foward or fall-back), or bulk data load)
- YugabyteDB Voyager version (the version of yb-voyager used for the migration)
- Source database details
  - Database type (PostgreSQL, Oracle, and MySQL)
  - Database version
  - Total size of the database migrating to YugabyteDB
- Target YugabyteDB database details
  - Database version
  - Node counts on the target YugabyteDB cluster
  - Total cores on the target YugabyteDB cluster
- Time taken in the migration phase up to the point when the payload is collected
- Status of the phase while the payload is sent (for example, In progress, Complete, Exit, and so on)
- IP address of the client machine on which voyager is running
- Anonymized DDL statements for schema objects (DDL definitions with all identifiers anonymized)

The payload for each migration phase is described in the following table. Note that no sensitive parameters, such as password-related arguments, are stored.

| Migration phase | Data collected |
| :---- | :------------- |
| <br/>[assess-migration](../assess-migration/) |<ul><li> Unsupported features (reported in the assessment report without any object names or SQL statements; only count of object per feature)</li> <li>Unsupported datatypes (reported in the assessment report without any table name or column name; only the datatype)</li> <li>Error message while running the assessment if any </li> <li>Table sizing statistics (reads/writes per second, and size in bytes)</li> <li>Index sizing statistics (reads/writes per second, and size in bytes)</li> <li>Schema summary (reported in the assessment report, without object names for each object type)</li> <li>Source connectivity (whether assessment is run with source connectivity) </li><li>Migration caveats (reported in the assessment report without any object name or SQL statements; only count of object per caveat) </li><li>Unsupported query constructs (reported in the assessment report without any query information; only count of queries per construct)</li></ul>|
| <br/>[export-schema](../schema-migration/export-schema/) | <ul><li>Whether sharding recommendations from the assessment report were applied</li></ul> |
| <br/>[analyze-schema](../schema-migration/analyze-schema/) | <ul><li>Issues (issues reported by analyze-schema without the SQL statements)</li><li>Database objects (objects migrated in the migration without the object names per object type)</li></ul> |
| <br/>[import-schema](../schema-migration/import-schema/) | <ul><li>Error count (if there are errors while running the import schema). <li>Flag values if passed or not:<ul><li>Continue on error</li><li>Enable orafce</li><li>Ignore exist</li><li>Refresh mviews</li><li>Post snapshot import</ul></li></li></ul> |
| <br/>[export-data](../data-migration/export-data/#export-data) <br/> [export-data-from-source](../data-migration/export-data/#export-data) <br/> [export-data-from-target](../data-migration/export-data/#export-data-from-target) | <ul><li>Parallel jobs used </li> <li>Total rows (total number of rows of all tables exported) </li><li>Largest table rows (total number of rows of all tables exported)</li><li> Phase (Snapshot, Streaming, or Cutover)</li><li>Total exported events (in case of live migration these are the total exported events)</li><li> Events export rate (in case of live migration this is the rate of events exported for the last 3 minutes)</li> <li>Live migration workflow type (Normal, with fall-forward or fall-back)</li></ul> |
| <br/>[import-data](../data-migration/import-data/#import-data) <br/> [import-data-to-target](../data-migration/import-data/#import-data) <br/> [import&#8209;data&#8209;to&#8209;source&#8209;replica](../data-migration/import-data/#import-data-to-source-replica) <br/>[import-data-to-source](../data-migration/import-data/#import-data-to-source) | <ul><li>Parallel jobs used </li><li>Total rows (total number of rows of all tables imported) </li><li>Largest table rows (total number of rows of all tables imported)</li> <li>Phase (Snapshot, Streaming, or Cutover)</li><li>Total imported events (for live migration these are the total imported events)</li><li>Events import rate (for live migration this the rate of events imported for the last 3 minutes)</li> <li>Live migration workflow type (Normal, with fall-forward or fall-back)</li></ul> |
| <br/>[import-data-file](../bulk-data-load/import-data-file/) (bulk data load) | <ul><li>Parallel jobs used </li><li> Total size (total size of all tables bulk imported) </li><li>Largest table size (largest size of tables imported) </li> <li>File storage type (Local, AWS S3, GCS, or Azure blob storage) </li><li>Data file format parameters (for example, null-string, format type, and so on)</li></ul> |

## Example payloads

The following are examples of payloads that are collected during some migration phases:

### `assess-migration` command payload

```output.json
{
  "migration_uuid": "8d9d678c-f6d4-4253-b24f-e10e4a49cc31",
  "phase_start_time": "2024-07-08 16:30:21.237453",
  "collected_at": "2024-07-08 16:30:22.69088",
  "source_db_details": {
    "host": "",
    "db_type": "postgresql",
    "db_version": "14.1 (Debian 14.1-1.pgdg110+1)",
    "total_db_size_bytes": 31391744
  },
  "target_db_details": "",
  "yb_voyager_version": "1.7.2",
  "migration_phase": "assess-migration",
  "phase_payload": {
    "unsupported_features": [
      {
        "FeatureName": "GIST indexes",
        "ObjectCount": 3
      },
      {
        "FeatureName": "Constraint triggers",
        "ObjectCount": 0
      },
      {
        "FeatureName": "Inherited tables",
        "ObjectCount": 0
      },
      {
        "FeatureName": "Tables with Stored generated columns",
        "ObjectCount": 0
      }
    ],
    "unsupported_datatypes": ["point", "point"],
    "migration_caveats": [
      {
        "FeatureName": "Alter partitioned tables to add Primary Key",
        "ObjectCount": 2
      },
      {
        "FeatureName": "Foreign tables",
        "ObjectCount": 2
      },
      {
        "FeatureName": "Policies",
        "ObjectCount": 3
      },
      {
        "FeatureName": "Unsupported Data Types for Live Migration",
        "ObjectCount": 7
      },
      {
        "FeatureName": "Unsupported Data Types for Live Migration with Fall-forward/Fallback",
        "ObjectCount": 3
      }
    ],
    "unsupported_query_constructs": {
      "Advisory Locks": 2,
      "System Columns": 0,
      "XML Function": 1
    },
    "table_sizing_stats": [
      { "object_name": "XXX", "reads_per_second": 0, "writes_per_second": 0, "size_in_bytes": 0 },
      { "object_name": "XXX", "reads_per_second": 0, "writes_per_second": 0, "size_in_bytes": 8192 },
      { "object_name": "XXX", "reads_per_second": 0, "writes_per_second": 0, "size_in_bytes": 8192 },
      { "object_name": "XXX", "reads_per_second": 0, "writes_per_second": 0, "size_in_bytes": 1245184 },
      { "object_name": "XXX", "reads_per_second": 0, "writes_per_second": 0, "size_in_bytes": 0 },
      { "object_name": "XXX", "reads_per_second": 0, "writes_per_second": 0, "size_in_bytes": 3055616 },
      { "object_name": "XXX", "reads_per_second": 0, "writes_per_second": 0, "size_in_bytes": 4825088 },
      { "object_name": "XXX", "reads_per_second": 0, "writes_per_second": 0, "size_in_bytes": 2998272 }
    ],
    "index_sizing_stats": [
      { "object_name": "XXX", "reads_per_second": 0, "writes_per_second": 0, "size_in_bytes": 8192 },
      { "object_name": "XXX", "reads_per_second": 0, "writes_per_second": 0, "size_in_bytes": 8192 },
      { "object_name": "XXX", "reads_per_second": 0, "writes_per_second": 0, "size_in_bytes": 8192 },
      { "object_name": "XXX", "reads_per_second": 0, "writes_per_second": 0, "size_in_bytes": 8192 },
      { "object_name": "XXX", "reads_per_second": 0, "writes_per_second": 0, "size_in_bytes": 8192 },
      { "object_name": "XXX", "reads_per_second": 0, "writes_per_second": 0, "size_in_bytes": 1130496 },
      { "object_name": "XXX", "reads_per_second": 0, "writes_per_second": 0, "size_in_bytes": 1130496 },
      { "object_name": "XXX", "reads_per_second": 0, "writes_per_second": 0, "size_in_bytes": 1130496 }
    ],
    "schema_summary": {
      "DatabaseObjects": [
        { "ObjectType": "SCHEMA", "TotalCount": 1 },
        { "ObjectType": "SEQUENCE", "TotalCount": 3 },
        { "ObjectType": "TABLE", "TotalCount": 9 },
        { "ObjectType": "INDEX", "TotalCount": 9 }
      ]
    },
    "source_connectivity": true
  },
  "migration_type": "",
  "time_taken_sec": 2,
  "status": "COMPLETE"
}
```

### `export-data` command payload

```output.json
{
    "migration_uuid": "8d9d678c-f6d4-4253-b24f-e10e4a49cc31",
    "phase_start_time": "2024-07-08 16:28:59.642321",
    "collected_at": "2024-07-08 16:29:05.986381",
    "source_db_details": {
        "host": "",
        "db_type": "postgresql",
        "db_version": "14.1 (Debian 14.1-1.pgdg110+1)",
        "total_db_size_bytes": 31391744
    },
    "target_db_details": "",
    "yb_voyager_version": "1.7.2",
    "migration_phase": "export-data",
    "phase_payload": {
        "parallel_jobs": 4,
        "total_rows_exported": 173641,
        "largest_table_rows_exported": 149000,
        "start_clean": true,
        "export_snapshot_mechanism": "pg_dump",
        "phase": "SNAPSHOT"
    },
    "migration_type": "offline",
    "time_taken_sec": 7,
    "status": "COMPLETE"
}
```

### `import-data` command payload

```output.json
{
  "migration_uuid": "8d9d678c-f6d4-4253-b24f-e10e4a49cc31",
  "phase_start_time": "2024-07-08 16:40:47.152743",
  "collected_at": "2024-07-08 16:42:03.363395",
  "source_db_details": "",
  "target_db_details": {
    "host": "",
    "db_version": "11.2-YB-2.18.2.1-b0",
    "node_count": 1,
    "total_cores": 8
  },
  "yb_voyager_version": "1.7.2",
  "migration_phase": "import-data",
  "phase_payload": {
    "parallel_jobs": 2,
    "total_rows_imported": 173641,
    "largest_table_rows_imported": 149000,
    "start_clean": true,
    "phase": "SNAPSHOT"
  },
  "migration_type": "offline",
  "time_taken_sec": 77,
  "status": "COMPLETE"
}
```

## Configure diagnostics collection

To control whether to send diagnostics to Yugabyte, you can use the `--send-diagnostics` flag or export an environment variable.

Set the flag as follows:

```sh
yb-voyager ... --send-diagnostics [true|false|yes|no|1|0]
```

The default is true.

Alternatively, you can use the following environment variable to set a value for this flag globally on the client machine where yb-voyager is running.

```sh
export YB_VOYAGER_SEND_DIAGNOSTICS=[true|false|yes|no|1|0]
```

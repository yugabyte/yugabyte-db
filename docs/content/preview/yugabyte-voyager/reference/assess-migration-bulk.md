---
title: assess migration bulk reference
headcontent: yb-voyager assess-migration-bulk
linkTitle: Bulk assess migration
description: YugabyteDB Voyager bulk assess migration reference
menu:
  preview_yugabyte-voyager:
    identifier: assess-migration-bulk-voyager
    parent: yb-voyager-cli
    weight: 2
badges: tp
type: docs
---

[Assess the migration](../../migrate/assess-migration) from multiple source (Oracle) database schemas to YugabyteDB.

## Syntax

```text
Usage: yb-voyager assess-migration-bulk [ <arguments> ... ]
```

### Arguments

The valid *arguments* for assess migration are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| &#8209;&#8209;bulk&#8209;assessment&#8209;dir | Directory path where assessment data is output. |
| --continue-on-error | Print the error message to the console and continue to next schema assessment.<br>Default: true.<br>Accepted parameters: true, false, yes, no, 0, 1. |
| --fleet-config-file | Path to the CSV file with connection parameters for schema(s) to be assessed. The first line must be a header row. <br>Fields (case-insensitive): 'source-db-type', 'source-db-host', 'source-db-port', 'source-db-name', 'oracle-db-sid', 'oracle-tns-alias', 'source-db-user', 'source-db-password', 'source-db-schema'.<br>Mandatory: 'source-db-type', 'source-db-user', 'source-db-schema', and one of ['source-db-name', 'oracle-db-sid', 'oracle-tns-alias']. |
| -h, --help | Command line help. |
| --send-diagnostics | Enable or disable sending [diagnostics](../../diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --start-clean | Cleans up the project directory for schema or data files depending on the export command. <br>Default: false <br> Accepted parameters: true, false, yes, no, 0, 1. |
| -y, --yes | Assume answer to all prompts during migration. <br>Default: false |

## Example

```sh
yb-voyager assess-migration-bulk \
    --fleet-config-file /path/to/fleet_config_file.csv \
    --bulk-assessment-dir /path/to/bulk-assessment-dir \
    --continue-on-error true \
    --ignore-exists true \
    --start-clean true
```

The following is an example fleet configuration file.

```text
source-db-type,source-db-host,source-db-port,source-db-name,oracle-db-sid,oracle-tns-alias,source-db-user,source-db-password,source-db-schema
oracle,example-host1,1521,ORCL,,,admin,password,schema1
oracle,example-host2,1521,,ORCL_SID,,admin,password,schema2
oracle,example-host3,1521,,,tns_alias,oracle_user,password,schema3
```

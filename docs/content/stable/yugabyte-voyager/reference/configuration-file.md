---
title: Configuration file
headcontent: YugabyteDB Voyager configuration file reference
linkTitle: Configuration file
description: YugabyteDB Voyager configuration file reference.
menu:
  stable_yugabyte-voyager:
    identifier: voyager-configuration-file
    parent: reference-voyager
    weight: 101
type: docs
rightNav:
  hideH4: true
---

The YugabyteDB Voyager configuration file lets you define all the parameters required for a migration in one place, instead of passing flags through the command line repeatedly. You can use a **YAML**-based configuration file to simplify and standardize migrations across environments.

This feature is available in YugabyteDB Voyager v2025.6.2 or later.

You can pass the configuration file to any Voyager command using the `--config-file` flag. When this flag is used, Voyager reads parameters from the provided YAML file instead of the CLI flags and arguments.

## Parameter precedence

If the same parameter is provided both in the configuration file and CLI flag, the **CLI flag always takes precedence**. This allows you to override specific values from the configuration file without modifying the file itself — useful for testing, automation, or environment-specific overrides.

For example:

```yaml
# config.yaml
source:
  db-user: config_user
```

```bash
yb-voyager export schema --config-file config.yaml --source-db-user cli_user
```

In this case, `cli_user` (via CLI) overrides `config_user` (via configuration file), and CLI flag value is used for migration.

## Overview of configuration file structure

The configuration file groups parameters into logical sections based on their usage and scope:

* Global parameters are defined at the top level of the YAML file.
* The `source`, `source-replica`, and `target` sections contain database-specific configurations.
* Command-specific sections such as `export-schema`, `import-data`, and others, can override parameters for individual commands.

You can refer to the following configuration file templates:

* [offline-migration.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/offline-migration.yaml)
* [live-migration.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/live-migration.yaml)
* [live-migration-with-fall-back.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/live-migration-with-fall-back.yaml)
* [live-migration-with-fall-forward.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/live-migration-with-fall-forward.yaml)
* [bulk-data-load.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/bulk-data-load.yaml)

All supported configuration parameters are described in the following sections.

### Global parameters

```yaml
# Export directory is a local directory used to store the exported schema, data, migration state, logs, and reports.
export-dir: <export-dir-name>

# Log level for yb-voyager.
# Accepted values - (trace, debug, info, warn, error, fatal, panic)
# Default : info
log-level: info

# Enable or disable sending diagnostics to Yugabyte
# Accepted values - (true, false, yes, no, 1, 0)
# Default : true
send-diagnostics: true

# *********** Control Plane Configuration ************
# To see the Voyager migration workflow details in the UI, set the following parameters.

# Control plane type refers to the deployment type of YugabyteDB
# Accepted values: yugabyted
# Optional (if not set, no visualization will be available)
control-plane-type: yugabyted

# Yugabyted Control Plane Configuration (for local yugabyted clusters)
# Uncomment the section below if control-plane-type is 'yugabyted'
yugabyted-control-plane:
  # YSQL connection string to yugabyted database
  # Provide standard PostgreSQL connection parameters: user name, host name, and port
  # Example: postgresql://yugabyte:yugabyte@127.0.0.1:5433
  # Note: Don't include the dbname parameter; the default 'yugabyte' database is used for metadata
  db-conn-string: postgresql://yugabyte:yugabyte@127.0.0.1:5433
```

### Source database configuration

```yaml
source:
  # Source database type: (oracle, mysql, postgresql)
  db-type: postgresql

  # Source database server host
  # Default : localhost
  db-host: localhost

  # Source database server port number
  # Default : Oracle(1521), MySQL(3306), PostgreSQL(5432)
  db-port: 5432

  # Source database name to be migrated to YugabyteDB
  db-name: test_db

  # Source schema name to export
  # Note - Valid only for Oracle and PostgreSQL
  # In PostgreSQL, can be comma-separated: "schema1,schema2"
  db-schema: test_schema

  # Connect to source database as the specified user
  db-user: test_user

  # Source password to connect as the specified user
  # Can be specified via SOURCE_DB_PASSWORD environmental variable
  db-password: test_password

  # Comma-separated list of read replica endpoints.
  # Default port: 5432
  # Example: "host1, host2:5433"
  # Note - Valid only for PostgreSQL
  read-replica-endpoints: host:port

  # Path to source SSL Certificate
  ssl-cert: /path/to/cert

  # Source SSL mode
  # Accepted values - disable, allow, prefer, require, verify-ca, verify-full
  # Default : prefer
  ssl-mode: prefer

  # Path to source SSL Key
  ssl-key: /path/to/key

  # Path to source SSL Root Certificate
  ssl-root-cert: /path/to/root.crt

  # Path to source SSL Root Certificate Revocation List (CRL)
  ssl-crl: /path/to/crl

  # Oracle specific: Oracle System Identifier (SID)
  oracle-db-sid: ORCL

  # Oracle specific: Oracle home path
  oracle-home: /opt/oracle

  # Oracle specific: TNS alias
  oracle-tns-alias: ORCL_ALIAS

  # Oracle specific: Container DB name
  oracle-cdb-name: CDB

  # Oracle specific: Container DB SID
  oracle-cdb-sid: CDBSID

  # Oracle specific: Container DB TNS alias
  oracle-cdb-tns-alias: CDB_ALIAS
```

### Target database configuration

```yaml
target:
  # Host for YugabyteDB
  # Default : 127.0.0.1
  db-host: 127.0.0.1

  # Port for YugabyteDB YSQL API
  # Default : 5433
  db-port: 5433

  # YugabyteDB database name
  db-name: yugabyte

  # Target schema name
  # Note - Only for Oracle and MySQL sources
  db-schema: public

  # Username for YugabyteDB
  db-user: target_user

  # Password to connect to YugabyteDB
  # Can be specified via TARGET_DB_PASSWORD environmental variable
  db-password: target_password

  # Path to SSL Certificate
  ssl-cert: /path/to/target-cert

  # SSL mode for target
  # Accepted values - disable, allow, prefer, require, verify-ca, verify-full
  # Default : prefer
  ssl-mode: prefer

  # Path to SSL Key
  ssl-key: /path/to/target-key

  # Path to SSL Root Certificate
  ssl-root-cert: /path/to/target-root.crt

  # Path to CRL
  ssl-crl: /path/to/target-crl
```

### Source replica configuration

```yaml
source-replica:
  # Host for source-replica
  db-host: replica-host

  # Port for source-replica
  # Default : 1521 (Oracle)
  db-port: 1521

  # Replica DB name
  db-name: replica_db

  # Schema in the replica DB
  db-schema: replica_schema

  # Username for replica
  db-user: replica_user

  # Password to connect to replica DB
  # Can be specified via SOURCE_REPLICA_DB_PASSWORD environmental variable
  db-password: replica_password

  # Path to SSL Certificate
  ssl-cert: /path/to/replica-cert

  # SSL mode for replica
  ssl-mode: prefer

  # Path to SSL Key
  ssl-key: /path/to/replica-key

  # Path to SSL Root Certificate
  ssl-root-cert: /path/to/replica-root.crt

  # Path to CRL
  ssl-crl: /path/to/replica-crl

  # Oracle SID for replica
  db-sid: REPLSID

  # Oracle home for replica
  oracle-home: /opt/replica-oracle

  # TNS alias for replica
  oracle-tns-alias: REPL_ALIAS
```

### Command-specific parameters

Additionally, the configuration file can include command-specific sections like `export-schema`, `import-data`, `cutover`, and so on. These sections allow you to specify parameters applicable only to the corresponding command.

For a complete list of available command-specific keys, refer to the [command specific CLI references](../../reference/yb-voyager-cli/#commands).

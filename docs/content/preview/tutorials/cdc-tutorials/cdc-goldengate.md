---
title: Capture and replicate data with Oracle GoldenGate in YugabyteDB
headerTitle: Oracle GoldenGate
linkTitle: Oracle GoldenGate
description: Oracle GoldenGate for Change Data Capture in YugabyteDB.
headcontent: |
  Connect Oracle GoldenGate Classic to YugabyteDB to capture and replicate data in real-time.
menu:
  preview_tutorials:
    parent: tutorials-cdc
    identifier: cdc-oracle-goldengate
    weight: 30
type: docs
---

YugabyteDB has a PostgreSQL-compatible query layer that enables connectivity to Oracle GoldenGate using PostgreSQL PostgreSQL-compatible ODBC driver. This tutorial specifically covers Oracle GoldenGate 21c Classic, which is compatible with a PostgreSQL data store as both a source and target endpoint for data capture and delivery.

### Prerequisites

The following packages should be installed on the target YugabyteDB node where Oracle GoldenGate will run:

* Oracle Client
* unixODBC
* psqlodbc
* libnsl
* Libaio
* Postgresql14-contrib (required for capture)

### Install Oracle GoldenGate for YugabyteDB

1. Download the [Oracle GoldenGate software for PostgreSQL](https://www.oracle.com/in/middleware/technologies/goldengate-downloads.html) from the Oracle website with a valid licensed account.

1. Unzip the software to your chosen GoldenGate home directory on the YugabyteDB node.

    {{<tip title="Single-node connectivity">}}
As the ODBC driver supports only single-node connectivity, place the GoldenGate software on the YugabyteDB node intended for data capture and delivery.
    {{</tip>}}

### Configure ODBC driver for YugabyteDB

Oracle GoldenGate supports connectivity via an ODBC driver. So, set up the ODBC configuration file `odbc.ini` by providing appropriate data source name [DSN] details as follows:

* [DSN] - A user-defined Data Source Name (for example, YB_src, YB_tgt).

* `InstallDir` - The absolute path to your Oracle GoldenGate home directory.

* `Driver` - The Full path to the GoldenGate PostgreSQL wire protocol driver file.

* `Database` - The YugabyteDB namespace (database name) relevant to either your source or target connection.

* `HostName` - The IP address or hostname of the YugabyteDB node.

* `PortNumber` - The YSQL listening port to connect to the database (typically 5433).

For example,

```sh
[yugabyte@ip-10-98-41-158 ~]$ cat /etc/odbc.ini
#Sample DSN entries DSN entries for Oracle GoldenGate connectivity
[ODBC Data Sources]
PostgreSQL Wire Protocol=DataDirect 7.1 PostgreSQL Wire Protocol
YB_src=DataDirect 7.1 PostgreSQL Wire Protocol
YB_tgt=DataDirect 7.1 PostgreSQL Wire Protocol

[ODBC]
IANAAppCodePage=4
InstallDir=/home/yugabyte/gg_yb_21c/

[YB_src]
Driver=/home/yugabyte/gg_yb_21c/lib/GGpsql25.so
Description=DataDirect 7.1 PostgreSQL Wire Protocol
Database=yugadb1
HostName=10.98.41.158
PortNumber=5433

[YB_tgt]
Driver=/home/yugabyte/gg_yb_21c/lib/GGpsql25.so
Description=DataDirect 7.1 PostgreSQL Wire Protocol
Database=yugadb1
HostName=10.98.41.158
PortNumber=5433
```

#### Connecting to FIPS-enabled YugabyteDB (PostgreSQL compatible v14 or Lower)

When the Oracle GoldenGate Extract is run from a Federal Information Processing Standards (FIPS) enabled system installed with a PostgreSQL compatible database lower than version 14, you might encounter the following error:

```sh
ERROR OGG-25359 Could not connect to the server with database 'postgres', host 'localhost', port '5432' and user name 'postgres'. Error Message: connection to server at "localhost" (::1), port 5432 failed: could not encrypt password: disabled for FIPSfe_sendauth: error sending password authentication.
```

To ensure the extract process runs correctly on YugabyteDB, perform the following configuration steps:

1. Modify the `yb_pg_hba.conf` file to set the `password_encryption` option to `scram-sha-256`.
1. Modify the `ysql_hba_conf_csv` flag to set the `Method` option to `scram-sha-256`, as MD5 authentication is not supported on a FIPS-enabled system.

    ```sh
    ysql_hba_conf_csv=host yugadb1 all all scram-sha-256z
    ```

#### Set environment variables

Set the following critical environment variables to ensure Oracle GoldenGate and its components can locate necessary files and configurations:

* `ODBCINI`: This variable must point to the absolute path of your `odbc.ini` file.
* `LD_LIBRARY_PATH` : This variable must include both the Oracle client library path and the GoldenGate library path, separated by a colon (:).

For example,

```sh
export ODBCINI=/etc/odbc.ini
export LD_LIBRARY_PATH=/opt/oracle/client/lib:/opt/goldengate/lib
```

### Prepare YugabyteDB for capture and replication

This section outlines the necessary database-level preparations in YugabyteDB for Oracle GoldenGate operations.

#### Create GoldenGate user and grant privileges

1. Create a GoldenGate user. Oracle GoldenGate processes require a dedicated database user to capture and deliver data. It is strongly recommended to create a dedicated YugabyteDB user for both capture and replication processes.

    ```sql
    create user <username> password '<password>';
    ```

1. Grant permissions for Data Capture. These privileges enable the Oracle GoldenGate Extract process to read and capture changes from the specified database and schema.

    ```sql
    GRANT CONNECT ON DATABASE <dbname> TO <username>;
    ALTER USER <username> WITH SUPERUSER;
    ALTER USER <username> WITH REPLICATION;
    GRANT USAGE ON SCHEMA tableschema TO <username>;
    GRANT SELECT ON ALL TABLES IN SCHEMA tableschema TO <username>;
    ```

1. Grant permissions for data replication. These privileges allow the Oracle GoldenGate Replicat process to apply captured data changes (inserts, updates, deletes, truncates) to target tables within the specified database and schema.

    ```sql
    GRANT CONNECT ON DATABASE <dbname> TO <username>;
    GRANT USAGE ON SCHEMA <tableschema> TO <username>;
    GRANT SELECT ON ALL TABLES IN SCHEMA <tableschema> TO <username>;
    GRANT INSERT, UPDATE, DELETE, TRUNCATE ON TABLE <tablename> TO <username>;
    ```

1. Heartbeat and checkpoint table privileges. Provide permissions for Oracle GoldenGate to manage its internal heartbeat and checkpoint tables, which are crucial to track replication progress and ensure recovery.

    ```sql
    GRANT CREATE ON DATABASE <dbname> TO <username>;
    GRANT CREATE, USAGE ON SCHEMA <tableschema> TO <username>;
    GRANT EXECUTE ON ALL FUNCTIONS IN SCHEMA <tableschema> TO <username>;
    GRANT SELECT, INSERT, UPDATE, DELETE, ON ALL TABLES IN SCHEMA <tableschema> TO <username>;
    ```

#### Enable Database Parameters

The following PostgreSQL-compatible parameters in YugabyteDB are typically enabled by default for CDC scenarios. You can adjust their values as per your specific replication requirements.

```sh
wal_level = logical                     # Required to be set to 'logical' for Capture processes.
max_replication_slots = 1               # Defines the maximum number of replication slots; one slot per Extract or client connection.
max_wal_senders = 1                     # Specifies the maximum number of WAL sender processes; corresponds to 'max_replication_slots'.
track_commit_timestamp = on             # Optional; used to correlate transaction commit time with begin transaction log records, which is useful for timestamp-based positioning.
```

### Configure GoldenGate processes

Configure the core Oracle GoldenGate processes necessary for data capture and delivery using the following steps.

#### Manager Process

The Manager process is the central controlling entity of Oracle GoldenGate. It is responsible for managing other GoldenGate processes and files, resource allocation, user interface interactions, and error reporting.

To start the Manager process, connect to the GoldenGate Software Command Interface (`ggsci`) and execute the following commands:

```sh
./ggsci
view param mgr
```

Note that the default listening port for the `Manager` process is 7809. Ensure this port is open and available.

#### Enabling Table-Level supplemental logging

Enable table-level logging to support change data capture of source DML operations.

```sh
ADD trandata <schema>.<tablename> ALLCOLS
```

For example:

```sh
ADD trandata public.regions ALLCOLS
Info trandata public.regions
```

#### Extract Process (To capture changed data)

The Extract process is responsible for capturing data changes. It can be configured to capture either full data records or transactional data changes. Once captured, the Extract process sends this data to a trail file, which is then processed by a downstream component like a data-pump Extract or the Replicat process.

Use the following commands to create and manage Extract groups:

```sql
GGSCI> register <extname>

# Sample Extract process configuration:

EXTRACT <extname>
SOURCEDB YB_tgt USERIDALIAS <alias>
EXTTRAIL ./dirdat/ep
GETTRUNCATES
TABLE <schema>.<tablename>;


GGSCI> ADD EXTRACT <extname>, TRANLOG, BEGIN NOW
GGSCI> ADD EXTTRAIL ./dirdat/ep, EXTRACT <extname>
```

Example configuration and commands are as follows:

```sql
GGSCI> view param exte
EXTRACT exte
SOURCEDB YB_tgt USERIDALIAS yugacredalias
EXTTRAIL ./dirdat/ep
GETTRUNCATES
TABLE public.regions;

GGSCI> ADD EXTRACT exte, TRANLOG, BEGIN NOW
Extract added.

GGSCI> ADD exttrail ./dirdat/yt, extract exte
EXTTRAIL added.
```

#### Add CHECKPOINT table at target side

A checkpoint table is used by a Replicat in the target database for recovery positioning when restarting a Replicat. A checkpoint table is optional (but recommended) for a Classic Replicat and required for Coordinated and Parallel Replicats.

```sql
ADD CHECKPOINTTABLE <schema>.<checkpointtablename>
```

Example:

```sql
GGSCI> ADD CHECKPOINTTABLE ggpdbuser.ggorcheckpoint
```

#### Replicat Process (To deliver data)

The Replicat process reads data extracted by the Extract process and applies it to target tables or prepares it for use by another application, such as a load application.

Use the following Replicat commands to create and manage Replicat groups.

```sql
# Sample Replicat process configuration:

REPLICAT <repname>
SOURCEDEFS <dirdef/<deffilename.def>
SETENV (ODBCINI="/etc/odbc.ini")
SETENV (PGCLIENTENCODING="UTF8")
SETENV (NLS_LANG="AMERICAN_AMERICA.AL32UTF8")
TARGETDB YB_tgt USERIDALIAS <useralias>
BATCHSQL
GETTRUNCATES
DISCARDFILE ./dirrpt/diskg.dsc, purge
MAP <PDB>.<schema>.<tablename>, TARGET <schema>.<tablename>, COLMAP (COL1=col1, T1_NAME=t1_name);
```

Example configuration and commands:

```sql
GGSCI> view param rep7tab
REPLICAT rep7tab
SOURCEDEFS dirdef/all7tab.def
SETENV (ODBCINI="/etc/odbc.ini")
SETENV (PGCLIENTENCODING="UTF8")
SETENV (NLS_LANG="AMERICAN_AMERICA.AL32UTF8")
TARGETDB YB_tgt USERIDALIAS yugacredalias
BATCHSQL
GETTRUNCATES
DISCARDFILE ./dirrpt/diskg.dsc, purge
MAP ORCLPDB1.ggpdbuser.regions, TARGET public.regions;

GGSCI> ADD CHECKPOINTTABLE public.ggcheckpoint
GGSCI> add replicat repil7tb, exttrail ./dirdat/il checkpointtable public.ggcheckpoint
```

### Start GoldenGate Processes

All Oracle GoldenGate processes are autonomous and, once configured, can be started and run independently.

#### Start Manager

The manager process must be running on different ports on source and destination if you are set up capture and replication between two distinct systems.

```sh
GGSCI > start mgr
GGSCI > info mgr
```

#### Start Extract

To begin capturing changes, start the configured Extract process:

```sh
GGSCI > Start <extname>
GGSCI > start exte
```

#### Start Replicat

To begin applying captured changes to the target, start the Replicat process:

```sql
GGSCI > add replicat <repname>, NODBCHECKPOINT, exttrail ./dirdat/yb
GGSCI > start <repname>
GGSCI > Start repr
```

### Learn more

* [Seamless Data Replication from Oracle to YugabyteDB With GoldenGate](https://www.yugabyte.com/blog/data-replication-to-yugabytedb-with-goldengate/)

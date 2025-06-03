---
title: Capture and Replicate Data with Oracle GoldenGate in YugabyteDB
headerTitle: Oracle GoldenGate
linkTitle: Oracle GoldenGate
description: Oracle GoldenGate for Change Data Capture in YugabyteDB.
headcontent: |
  This tutorial describes how to connect Oracle GoldenGate Classic to YugabyteDB to capture and replicate data in real-time.
menu:
  preview_tutorials:
    parent: tutorials-cdc
    identifier: cdc-oracle-goldengate
    weight: 30
type: docs
---

This tutorial describes how to connect Oracle GoldenGate Classic to YugabyteDB to capture and replicate data in real-time.

YugabyteDB has a PostgreSQL-compatible query layer that enables connectivity to Oracle GoldenGate using PostgreSQL PostgreSQL-compatible ODBC driver.  This tutorial is specific to Oracle GoldenGate 21c classic version ,which is compatible with the PostgreSQL data store as a  source and target endpoint to capture and deliver.

### Prerequisites

The following packages should be installed on the target side YugabyteDB node :

* Oracle Client
* unixODBC
* psqlodbc
* libnsl
* Libaio
* Postgresql14-contrib - For capture

### Install Oracle GoldenGate for YugabyteDB

Download the Oracle GoldenGate software for PostgreSQL from the Oracle website with a valid licensed account.

[https://www.oracle.com/in/middleware/technologies/goldengate-downloads.html](https://www.oracle.com/in/middleware/technologies/goldengate-downloads.html)

Unzip the software to the GoldenGate home directory on the YugabyteDB node.

Note ODBC supports only single-node connectivity. Therefore, place GoldenGate software on the Yugabyte node intended for data capture and delivery.,

#### Configure ODBC for YugabyteDB

Oracle GoldenGate supports connectivity via an ODBC driver. It is required to set up the ODBC configuration by providing appropriate data source name [DSN] details.

Create `odbc.ini` file withthe  following parameters :

[DSN] - Data Source Name

InstallDir - Oracle GoldenGate home directory

Driver - Full path of GoldenGate PostgreSQL wire protocol driver file

Database - YugabyteDB Namespace for source or target database

HostName - YugabyteDB node IP or hostname

PortNumber - YSQL listening port to connect to the database

```sh
[yugabyte@ip-10-98-41-158 ~]$ cat /etc/odbc.ini
#Sample DSN entries
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
[yugabyte@ip-10-98-41-158 ~]$
```

#### Connecting to YugabyteDB, a FIPS-enabled PostgreSQL compatible System with Version 14 or Lower

When the Oracle GoldenGate Extract is run from a Federal Information Processing Standards (FIPS) enabled system installed with a PostgreSQL compatible database lower than version 14, it generates the following error:

```sh
ERROR OGG-25359 Could not connect to the server with database 'postgres', host 'localhost', port '5432' and user name 'postgres'. Error Message: connection to server at "localhost" (::1), port 5432 failed: could not encrypt password: disabled for FIPSfe_sendauth: error sending password authentication.
```

To run Extract on YugabyteDB, perform the following steps:

1. Modify the yb_pg_hba.conf file to set the password_encryption option to scram-sha-256.
2. Modify the ysql_hba_conf_csv G-Flag to set the Method option to scram-sha-256, as MD5 is not supported on a FIPS-enabled system.

```sh
ysql_hba_conf_csv=host yugadb1 all all scram-sha-256z
```

#### Set Environment Variables

Variable Name - ODBCINI
Value - path of odbc.ini file
Variable Name  - LD_LIBRARY_PATH
Value - Oracle client lib path and GoldenGate lib path

```sh
export ODBCINI=/etc/odbc.ini
export LD_LIBRARY_PATH=/opt/oracle/client/lib:/opt/goldengate/lib
```

### Prepare the YugabyteDB for Capture and replicate

#### Create GoldenGate user and grant privileges

1. Oracle GoldenGate processes require a database user to capture and deliver data to a database, and it is recommended to create a dedicated Yugabyte database user for Extract and Replicat.

    ```sql
    create user <username> password '<password>';
    ```

    Example :

    ```sql
    create user ggpguser password 'gguser';
    ```

2. Grant permissions to capture

    ```sql
    GRANT CONNECT ON DATABASE <dbname> TO <username>;
    ALTER USER <username> WITH SUPERUSER;
    ALTER USER <username> WITH REPLICATION;
    GRANT USAGE ON SCHEMA tableschema TO <username>;
    GRANT SELECT ON ALL TABLES IN SCHEMA tableschema TO <username>;
    ```

    Example :

    ```sql
    GRANT CONNECT ON DATABASE yugadb TO gguser;
    ALTER USER gguser WITH SUPERUSER;
    ALTER USER gguser WITH REPLICATION;
    GRANT USAGE ON SCHEMA tableschema TO gguser;
    GRANT SELECT ON ALL TABLES IN SCHEMA tableschema TO gguser;
    ```

3. Grant permissions to replicate

    ```sql
    GRANT CONNECT ON DATABASE <dbname> TO <username>;
    GRANT USAGE ON SCHEMA tableschema TO <username>;
    GRANT SELECT ON ALL TABLES IN SCHEMA tableschema TO <username>;
    GRANT INSERT, UPDATE, DELETE, TRUNCATE ON TABLE tablename TO <username>;
    ```

    Example :

    ```sql
    GRANT CONNECT ON DATABASE yugadb TO gguser;
    GRANT USAGE ON SCHEMA tableschema TO gguser;
    GRANT SELECT ON ALL TABLES IN SCHEMA tableschema TO gguser;
    GRANT INSERT, UPDATE, DELETE, TRUNCATE ON TABLE tablename TO gguser;
    ```

4. Heartbeat and Checkpoint Table Privileges

    ```sql
    GRANT CREATE ON DATABASE dbname TO <username>;
    GRANT CREATE, USAGE ON SCHEMA ggschema TO <username>;
    GRANT EXECUTE ON ALL FUNCTIONS IN SCHEMA ggschema TO <username>;
    GRANT SELECT, INSERT, UPDATE, DELETE, ON ALL TABLES IN SCHEMA ggschema TO <username>;
    ```

    Example:

    ```sql
    GRANT CREATE ON DATABASE dbname TO gguser;
    GRANT CREATE, USAGE ON SCHEMA ggschema TO gguser;
    GRANT EXECUTE ON ALL FUNCTIONS IN SCHEMA ggschema TO gguser;
    GRANT SELECT, INSERT, UPDATE, DELETE, ON ALL TABLES IN SCHEMA ggschema TO gguser;
    ```

#### Enable Database Parameters

In YugabyteDB below parameters are already enabled, These can be set as per requirement.

```sh
wal_level = logical                     # set to logical for Capture
max_replication_slots = 1               # max number of replication slots,
                                        # one slot per Extract/client
max_wal_senders = 1                     # one sender per max repl slot
track_commit_timestamp = on             # optional, correlates tx commit time
                                        # with begin tx log record(useful for          ,timestamp-based positioning)                    
```

### Configure GoldenGate processes

#### Manager Process

Connect to GoldenGate Software command interface (ggsci) and start the manager process, which is the parent process of Oracle GoldenGate and is responsible for the management of its processes and files, resources, user interface, and the reporting of thresholds and errors.

```sh
./ggsci
view param mgr
```

Note: The Default port for the manager process is 7809.

#### Enabling Table-Level Supplemental Logging

Table level logging needs to be enabled to support change data capture of source DML operations.

```sh
ADD trandata <schema>.<tablename> ALLCOLS
```

Example:

```sh
ADD trandata public.regions ALLCOLS
Info trandata public.regions
```

#### Extract Process (To capture changed data)

Use the Extract commands to create and manage Extract groups. The Extract process

captures either full data records or transactional data changes, depending on configuration

parameters, and then sends the data to a trail for further processing by a downstream

process, such as a data-pump Extract or the Replicat process.

```sql
GGSCI> register <extname>

Sample extract process :

EXTRACT <extname>
SOURCEDB YB_tgt USERIDALIAS <alias>
EXTTRAIL ./dirdat/ep
GETTRUNCATES
TABLE <schema>.<tablename>;


GGSCI> ADD EXTRACT <extname>, TRANLOG, BEGIN NOW
GGSCI> ADD EXTTRAIL ./dirdat/ep, EXTRACT <extname>
```

Example:

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

#### **Add CHECKPOINT Table at target side**

A checkpoint table is used by a Replicat in the target database for recovery positioning when restarting a Replicat. A checkpoint table is optional (but recommended) for a Classic Replicat and required for Coordinated and Parallel Replicats.

```sql
ADD CHECKPOINTTABLE <schema>.<checkpointtablename>
```

Example:

```sql
GGSCI> ADD CHECKPOINTTABLE ggpdbuser.ggorcheckpoint
```

#### **Replicat Process (To deliver data)**

Use the Replicat commands to create and manage Replicat groups. The Replicat
process reads data extracted by the Extract process and applies it to target tables
or prepares it for use by another application, such as a load application.

```sql
Sample replication process :
 
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

Example:

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

All processes in GoldenGate are autonomous and can run independently once set up.

#### Start Manager

The manager process must be running on different ports on source and destination.

```sh
GGSCI > start mgr
GGSCI > info mgr 
```

#### Start Extract

```sh
GGSCI > Start <extname> 
GGSCI > start exte
```

#### Start Replicat

```sql
GGSCI > add replicat <repname>, NODBCHECKPOINT, exttrail ./dirdat/yb
GGSCI > start <repname>
GGSCI > Start repr
```

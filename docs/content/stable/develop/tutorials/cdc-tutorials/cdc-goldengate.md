---
title: Oracle GoldenGate tutorial for replicating data from Oracle to YugabyteDB
headerTitle: Oracle GoldenGate
linkTitle: Oracle GoldenGate
description: Replicate data from Oracle Database to YugabyteDB in real time using Oracle GoldenGate.
headcontent: Replicate data from Oracle Database to YugabyteDB using Oracle GoldenGate
menu:
  stable_develop:
    parent: tutorials-kafka-stream
    identifier: cdc-oracle-goldengate
    weight: 50
type: docs
---

[Oracle GoldenGate](https://www.oracle.com/integration/goldengate/) is a data replication tool that captures changes from a source database and applies them to a target database in real time. Because YugabyteDB's YSQL API is compatible with PostgreSQL, you can use GoldenGate for PostgreSQL to connect to YugabyteDB as the target and replicate data from an Oracle Database source with minimal downtime during a migration.

This tutorial describes how to set up real-time replication from Oracle Database to YugabyteDB. It uses the following setup, which you can adapt to your environment:

| Component | Source | Target |
| :--- | :--- | :--- |
| Database | Oracle Database 19c | YugabyteDB |
| GoldenGate | Oracle GoldenGate 21c for Oracle Database | Oracle GoldenGate 21c for PostgreSQL |
| Example schema and tables | Schema `ggpdbuser` in pluggable database `ORCLPDB1` | Database `yugadb1`, schema `public` |

GoldenGate connects to YugabyteDB over the PostgreSQL wire protocol using the ODBC driver bundled with GoldenGate for PostgreSQL. For other GoldenGate releases, check the [Oracle GoldenGate certification matrix](https://www.oracle.com/middleware/technologies/goldengate-downloads.html) for supported PostgreSQL-compatible targets before you start.

## Before you begin

Make sure you have the following:

- An Oracle Database source with the tables you want to replicate. This tutorial uses the tables `regions`, `locations`, `countries`, `departments`, `employees`, `job_history`, and `jobs` in the schema `ggpdbuser`.
- A running YugabyteDB cluster. For installation options, see [Deploy YugabyteDB](../../../../deploy/).
- GoldenGate installed on the source and target hosts (or on dedicated hosts in production):
  - Download [Oracle GoldenGate for Oracle Database and for PostgreSQL](https://www.oracle.com/middleware/technologies/goldengate-downloads.html) using a licensed Oracle account, and unzip each build to your chosen GoldenGate home directory.
  - On the target host, install the Oracle client libraries required by GoldenGate and make sure the GoldenGate library directory is included in `LD_LIBRARY_PATH`, as described in [Set up the target](#set-up-the-target).
- The target tables created in YugabyteDB before you start replication.

{{< note title="Create the target tables first" >}}
GoldenGate does not migrate table structures between heterogeneous databases. Create the equivalent tables in YugabyteDB (converting Oracle data types as needed) and give each table a primary key so that GoldenGate can apply updates and deletes and avoid conflicts during replication.
{{< /note >}}

## Set up the source

Perform the following steps on the Oracle source host using GoldenGate for Oracle Database.

### Create the GoldenGate user and enable logging

1. Create a GoldenGate administration user and grant it the required privileges. For a container database, use a common user:

    ```sql
    CREATE USER c##ggadmin IDENTIFIED BY ggadmin;
    GRANT resource, create session, create view, create table TO c##ggadmin;
    GRANT connect TO c##ggadmin CONTAINER=all;
    GRANT alter system, alter user TO c##ggadmin;
    ALTER USER c##ggadmin SET container_data=all CONTAINER=current;
    GRANT select any dictionary, select any transaction TO c##ggadmin;
    EXEC dbms_goldengate_auth.grant_admin_privilege('c##ggadmin', container=>'all');
    ```

1. Enable supplemental logging and GoldenGate replication for the database:

    ```sql
    ALTER DATABASE ADD SUPPLEMENTAL LOG DATA;
    ALTER SYSTEM SET enable_goldengate_replication=TRUE;
    ```

1. Enable table-level supplemental logging for each source table using `ADD TRANDATA` from GGSCI. For example:

    ```output
    GGSCI> ADD TRANDATA ORCLPDB1.ggpdbuser.regions ALLCOLS
    ```

### Generate the definitions file

When the source and target table structures differ, the Replicat process needs a definitions file that describes the source tables. Generate it on the source and copy it to the target GoldenGate home (for example, as `dirdef/all7tab.def`):

```sh
$ cat dirprm/defgen.prm
DEFSFILE ./dirprm/all7tab.def
USERIDALIAS oracredcdb
TABLE ORCLPDB1.ggpdbuser.regions;
TABLE ORCLPDB1.ggpdbuser.locations;
TABLE ORCLPDB1.ggpdbuser.countries;
TABLE ORCLPDB1.ggpdbuser.departments;
TABLE ORCLPDB1.ggpdbuser.employees;
TABLE ORCLPDB1.ggpdbuser.job_history;
TABLE ORCLPDB1.ggpdbuser.jobs;

$ ./defgen paramfile ./dirprm/defgen.prm
```

### Add credentials and start the Manager process

1. Add the source database credentials to the GoldenGate credential store, and verify that you can log in:

    ```output
    GGSCI> ADD CREDENTIALSTORE
    GGSCI> ALTER CREDENTIALSTORE ADD USER c##ggadmin PASSWORD <password> ALIAS oracredcdb
    GGSCI> DBLOGIN USERIDALIAS oracredcdb
    Successfully logged into database.
    ```

1. Set the Manager port in the `mgr` parameter file (the default is 7809) and start the Manager process:

    ```output
    GGSCI> VIEW PARAMS mgr
    PORT 7809

    GGSCI> START MGR
    Manager started.
    ```

## Set up the target

Perform the following steps on the YugabyteDB target host using GoldenGate for PostgreSQL.

### Configure the ODBC data source

GoldenGate for PostgreSQL connects to YugabyteDB using the bundled DataDirect PostgreSQL wire protocol ODBC driver. Define a data source name (DSN) for the target in the `odbc.ini` file. Note the following:

- `Driver` is the full path to the driver file (for example, `GGpsql25.so`) in your GoldenGate home.
- `Database` is the YugabyteDB database name, `HostName` is the address of a YugabyteDB node, and `PortNumber` is the YSQL port (5433 by default).

```sh
[yugabyte@target ~]$ cat /etc/odbc.ini
[ODBC Data Sources]
PostgreSQL Wire Protocol=DataDirect 7.1 PostgreSQL Wire Protocol
YB_tgt=DataDirect 7.1 PostgreSQL Wire Protocol

[ODBC]
IANAAppCodePage=4
InstallDir=/home/yugabyte/gg_yb_21c/

[YB_tgt]
Driver=/home/yugabyte/gg_yb_21c/lib/GGpsql25.so
Description=DataDirect 7.1 PostgreSQL Wire Protocol
Database=yugadb1
HostName=10.98.41.158
PortNumber=5433
```

The ODBC driver connects to a single YugabyteDB node. Run GoldenGate on (or close to) the node named in the DSN, and be prepared to update `HostName` if that node becomes unavailable.

### Set environment variables

```sh
export ODBCINI=/etc/odbc.ini
export LD_LIBRARY_PATH=/opt/oracle/client/lib:/home/yugabyte/gg_yb_21c/lib:$LD_LIBRARY_PATH
```

Replace the paths with the Oracle client library path and GoldenGate library path on your host.

### Prepare YugabyteDB and start the Manager process

1. Create a dedicated user for GoldenGate and grant it permission to apply changes to the target tables and to manage the checkpoint table:

    ```plpgsql
    CREATE USER ggadmin PASSWORD '<password>';
    GRANT CONNECT ON DATABASE yugadb1 TO ggadmin;
    GRANT CREATE, USAGE ON SCHEMA public TO ggadmin;
    GRANT SELECT, INSERT, UPDATE, DELETE, TRUNCATE ON ALL TABLES IN SCHEMA public TO ggadmin;
    ```

1. Add the target credentials to the GoldenGate credential store, log in using the DSN, and create the checkpoint table:

    ```output
    GGSCI> ADD CREDENTIALSTORE
    GGSCI> ALTER CREDENTIALSTORE ADD USER ggadmin PASSWORD <password> ALIAS yugacredalias
    GGSCI> DBLOGIN SOURCEDB YB_tgt, USERIDALIAS yugacredalias
    Successfully logged into database.

    GGSCI> ADD CHECKPOINTTABLE public.ggcheckpoint
    Successfully created checkpoint table public.ggcheckpoint.
    ```

1. Set the Manager port in the `mgr` parameter file and start the Manager process. Use a different port than the source Manager (for example, 7811) if both run on hosts managed together:

    ```output
    GGSCI> VIEW PARAMS mgr
    PORT 7811

    GGSCI> START MGR
    Manager started.
    ```

## Configure the Extract processes

Configure the following Extract processes on the source host.

### Initial-load Extract

Create an initial-load Extract that reads the current table contents and writes them to a remote file for the initial-load Replicat:

```output
GGSCI> VIEW PARAMS expil7tb
EXTRACT expil7tb
USERIDALIAS oracredcdb
RMTHOST 10.98.41.158, MGRPORT 7811
RMTFILE ./dirdat/il, MAXFILES 999999, MEGABYTES 190, APPEND
TABLE ORCLPDB1.ggpdbuser.regions;
TABLE ORCLPDB1.ggpdbuser.locations;
TABLE ORCLPDB1.ggpdbuser.countries;
TABLE ORCLPDB1.ggpdbuser.departments;
TABLE ORCLPDB1.ggpdbuser.employees;
TABLE ORCLPDB1.ggpdbuser.job_history;
TABLE ORCLPDB1.ggpdbuser.jobs;

GGSCI> ADD EXTRACT expil7tb, SOURCEISTABLE
Extract added.
```

Before you start the initial load, record the current system change number (SCN). You use it to start the real-time Replicat from a consistent point:

```sql
SELECT current_scn FROM v$database;
```

### Change data capture Extract

Create an integrated Extract that captures ongoing changes and writes them to a remote trail:

```output
GGSCI> VIEW PARAMS ext7tab
EXTRACT ext7tab
USERIDALIAS oracredcdb
RMTHOST 10.98.41.158, MGRPORT 7811
RMTTRAIL ./dirdat/rt
DDL INCLUDE MAPPED
LOGALLSUPCOLS
UPDATERECORDFORMAT COMPACT
TABLE ORCLPDB1.ggpdbuser.regions;
TABLE ORCLPDB1.ggpdbuser.locations;
TABLE ORCLPDB1.ggpdbuser.countries;
TABLE ORCLPDB1.ggpdbuser.departments;
TABLE ORCLPDB1.ggpdbuser.employees;
TABLE ORCLPDB1.ggpdbuser.job_history;
TABLE ORCLPDB1.ggpdbuser.jobs;

GGSCI> ADD EXTRACT ext7tab, INTEGRATED TRANLOG, BEGIN NOW
Integrated Extract added.

GGSCI> REGISTER EXTRACT ext7tab, DATABASE CONTAINER(ORCLPDB1)
```

## Configure the Replicat processes

Configure the following Replicat processes on the target host. Both use the definitions file you copied from the source and map each source table to the corresponding target table.

### Initial-load Replicat

```output
GGSCI> VIEW PARAMS repil7tb
REPLICAT repil7tb
SOURCEDEFS dirdef/all7tab.def
SETENV (ODBCINI="/etc/odbc.ini")
SETENV (PGCLIENTENCODING="UTF8")
SETENV (NLS_LANG="AMERICAN_AMERICA.AL32UTF8")
TARGETDB YB_tgt USERIDALIAS yugacredalias
DISCARDFILE ./dirrpt/diskg.dsc, PURGE
MAP ORCLPDB1.ggpdbuser.regions, TARGET public.regions;
MAP ORCLPDB1.ggpdbuser.locations, TARGET public.locations;
MAP ORCLPDB1.ggpdbuser.countries, TARGET public.countries;
MAP ORCLPDB1.ggpdbuser.departments, TARGET public.departments;
MAP ORCLPDB1.ggpdbuser.employees, TARGET public.employees;
MAP ORCLPDB1.ggpdbuser.job_history, TARGET public.job_history;
MAP ORCLPDB1.ggpdbuser.jobs, TARGET public.jobs;

GGSCI> ADD REPLICAT repil7tb, EXTTRAIL ./dirdat/il CHECKPOINTTABLE public.ggcheckpoint
Replicat added.
```

### Real-time Replicat

```output
GGSCI> VIEW PARAMS rep7tab
REPLICAT rep7tab
SOURCEDEFS dirdef/all7tab.def
SETENV (ODBCINI="/etc/odbc.ini")
SETENV (PGCLIENTENCODING="UTF8")
SETENV (NLS_LANG="AMERICAN_AMERICA.AL32UTF8")
TARGETDB YB_tgt USERIDALIAS yugacredalias
BATCHSQL
GETTRUNCATES
DISCARDFILE ./dirrpt/diskg.dsc, PURGE
MAP ORCLPDB1.ggpdbuser.regions, TARGET public.regions;
MAP ORCLPDB1.ggpdbuser.locations, TARGET public.locations;
MAP ORCLPDB1.ggpdbuser.countries, TARGET public.countries;
MAP ORCLPDB1.ggpdbuser.departments, TARGET public.departments;
MAP ORCLPDB1.ggpdbuser.employees, TARGET public.employees;
MAP ORCLPDB1.ggpdbuser.job_history, TARGET public.job_history;
MAP ORCLPDB1.ggpdbuser.jobs, TARGET public.jobs;

GGSCI> ADD REPLICAT rep7tab, EXTTRAIL ./dirdat/rt CHECKPOINTTABLE public.ggcheckpoint
Replicat added.
```

## Start replication and verify

1. On the source, start the initial-load Extract and the change data capture Extract:

    ```output
    GGSCI> START expil7tb
    GGSCI> START ext7tab
    ```

1. On the target, start the initial-load Replicat to load the existing data into YugabyteDB:

    ```output
    GGSCI> START repil7tb
    ```

1. After the initial load completes, start the real-time Replicat from the SCN you recorded before the initial load:

    ```output
    GGSCI> START rep7tab, AFTERCSN 3625429
    ```

    Replace `3625429` with the SCN from your environment.

1. Verify that replication is working:
    - On both hosts, run `INFO ALL` in GGSCI and confirm that the Manager, Extract, and Replicat processes are in `RUNNING` state.
    - Compare row counts for a replicated table in Oracle and in YugabyteDB.
    - Insert or update a row in Oracle and confirm that the change appears in YugabyteDB:

        ```plpgsql
        SELECT * FROM public.regions;
        ```

## Learn more

- [Seamless data replication from Oracle to YugabyteDB with GoldenGate](https://www.yugabyte.com/blog/data-replication-to-yugabytedb-with-goldengate/)
- [Change data capture](../../../../additional-features/change-data-capture/)

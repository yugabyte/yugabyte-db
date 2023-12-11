---
title: Configure audit logging in YSQL
headerTitle: Configure audit logging in YSQL
description: Configure audit logging in YSQL.
menu:
  preview:
    name: Configure audit logging
    identifier: enable-audit-logging-1-ysql
    parent: audit-logging
    weight: 755
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../audit-logging-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../audit-logging-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

YugabyteDB YSQL uses PostgreSQL Audit Extension ([pgaudit](https://www.pgaudit.org/)) to provide detailed session and/or object audit logging via YugabyteDB YB-TServer logging.

The goal of the YSQL audit logging is to provide YugabyteDB users with capability to produce audit logs often required to comply with government, financial, or ISO certifications. An audit is an official inspection of an individual's or organization's accounts, typically by an independent body.

## Enable audit logging

To enable audit logging, first configure audit logging for the cluster. This is done in one of the following ways:

- At database startup.

    Use the [--ysql_pg_conf_csv](../../../reference/configuration/yb-tserver/#ysql-pg-conf-csv) YB-TServer flag.

    Database administrators can use `ysql_pg_conf_csv` to configure audit logging with [pgaudit flags](#customize-audit-logging).

    Provide the options as a comma separated values. For example, `ysql_pg_conf_csv="pgaudit.log='DDL',pgaudit.log_level=notice"`.

    Use double quotes to enclose any settings that include commas.

    These configuration values are set when the YugabyteDB cluster is created and therefore apply for all users and for every session.

- Per session.

    Use the [SET](../../../api/ysql/the-sql-language/statements/cmd_set/) command in a running session.

    The `SET` command essentially changes the run-time configuration parameters.

    For example, `SET pgaudit.log='DDL'`

    `SET` only affects the value used by the current session. For more information, see the [PostgreSQL documentation](https://www.postgresql.org/docs/11/sql-set.html).

### Create the extension

After configuring the YB-TServer and starting the cluster, create the `pgaudit` extension by executing the following statement in ysqlsh:

```sql
CREATE EXTENSION IF NOT EXISTS pgaudit;
```

You only need to run this statement on a single node, and it will apply across your cluster.

## Customize audit logging

You can customize YSQL audit logging using the `pgaudit` flags, as per the following table.

| Option | Description | Default |
| :----- | :----- | :------ |
| pgaudit.log | Specifies which classes of statements are logged by session audit logging, as follows:<ul><li>**READ**: SELECT and COPY when the source is a relation or a query.<li>**WRITE**: INSERT, UPDATE, DELETE, TRUNCATE, and COPY when the destination is a relation.<li>**FUNCTION**: Function calls and DO blocks.<li>**ROLE**: Statements related to roles and privileges: GRANT, REVOKE, CREATE/ALTER/DROP ROLE.<li>**DDL**: All DDL that is not included in the ROLE class.<li>**MISC**: Miscellaneous commands, such as DISCARD, FETCH, CHECKPOINT, VACUUM, SET.<li>**MISC_SET**: Miscellaneous SET commands, such as SET ROLE.<li>**ALL**: Include all of the preceding options.</ul>You can specify multiple classes using a comma-separated list. Subtract classes by prefacing the class with a minus (`-`) sign. | none |
| pgaudit.log_catalog | Log statements for the PostgreSQL system catalog relations in `pg_catalog`. These system catalog tables record system (as opposed to user) activity, such as metadata lookups and from third-party tools performing lookups.<br>These statements aren't required for typical auditing and you can disable this option to reduce noise in the log. | ON |
| pgaudit.log_client | Enable this option to echo log messages directly to clients such as ysqlsh an psql. Log messages are printed directly to the shell, which can be helpful for debugging. When enabled, you can set the level of logs that are output using `pgaudit.log_client`. | OFF |
| pgaudit.log_level | Sets the [severity level](https://www.postgresql.org/docs/16/runtime-config-logging.html#RUNTIME-CONFIG-SEVERITY-LEVELS) of logs written to clients when `pgaudit.log_client` is on.<br>Values: DEBUG1 .. DEBUG5, INFO, NOTICE, WARNING, LOG. ERROR, FATAL, and PANIC are not allowed. Use this setting for debugging and testing.<br>`pgaudit.log_level` only applies when `pgaudit.log_client` is on; otherwise the default LOG level is used. | LOG |
| pgaudit.log_parameter | Include the parameters that were passed with the statement in the logs. When parameters are present, they are included in CSV format after the statement text. | OFF |
| pgaudit.log_parameter_max_size | Specifies the size, in bytes, of parameters to include in the logs if `pgaudit.log_parameter` is on. Parameters longer than this value are not logged and replaced with `<long param suppressed>`. The default of 0 indicates that all parameters are logged regardless of length. | 0 |
| pgaudit.log_relation | Create separate log entries for each relation (TABLE, VIEW, and so on) referenced in a SELECT or DML statement. This is a shortcut for exhaustive logging without using [object audit logging](../object-audit-logging-ysql/). | OFF |
| pgaudit.log_statement_once | Include the statement text and parameters for a statement or sub-statement combination with the first log entry only. Ordinarily, statement text and parameters are included with every log entry. Enable this setting for less verbose logging; however, this can make it more difficult to determine the statement that generated a log entry. | OFF |
| pgaudit.role | Specifies the master role to use for object audit logging. To define multiple audit roles, grant the roles to the master role; this allows multiple groups to be in charge of different aspects of audit logging. | None |
<!--
| pgaudit.log_rows | Include the rows retrieved or affected by a statement. The rows field is included after the parameter field. | OFF |
| pgaudit.log_statement | Include the statement text and parameters. Depending on requirements, an audit log might not require this and it makes the logs less verbose. | ON |
-->

## Examples

{{% explore-setup-single %}}

Using ysqlsh, connect to the database and enable the `pgaudit` extension on the YugabyteDB cluster as follows:

```sql
\c yugabyte yugabyte;
CREATE EXTENSION IF NOT EXISTS pgaudit;
```

### Basic audit logging

In ysqlsh, execute the following commands:

```sql
SET pgaudit.log='DDL';
SET pgaudit.log_client=ON;
SET pgaudit.log_level=notice;
```

#### Create a table and verify the log

As `pgaudit.log='DDL'` is configured, `CREATE TABLE` YSQL statements are logged and the corresponding log is shown in ysqlsh:

```sql
CREATE TABLE employees (empno int, ename text, address text,
  salary int, account_number text);
```

```output
NOTICE:  AUDIT: SESSION,2,1,DDL,CREATE TABLE,TABLE,public.employees,
"create table employees ( empno int, ename text, address text, salary int,
account_number text );",<not logged>
CREATE TABLE
```

Notice that audit logs are generated for DDL statements.

### Advanced audit logging

For this example, start a new cluster with the following audit logging configuration:

```shell
--ysql_pg_conf_csv="log_line_prefix='%m [%p %l %c] %q[%C %R %Z %H] [%r %a %u %d] '",pgaudit.log='all',pgaudit.log_parameter=on,pgaudit.log_relation=on,pgaudit.log_catalog=off,suppress_nonpg_logs=on
```

Enable the `pgaudit` extension on any node as follows:

```sql
CREATE EXTENSION IF NOT EXISTS pgaudit;
CREATE TABLE IF NOT EXISTS my_table ( h int, r int, v int, primary key(h,r));
```

Start two sessions and execute transactions concurrently as follows:

<table class="no-alter-colors">
  <thead>
    <tr>
    <th>
    Client 1
    </th>
    <th>
    Client 2
    </th>
    </tr>
  </thead>
  <tbody>
  <tr>
   <td>

```sql
BEGIN;
INSERT INTO my_table VALUES (5,2,2);
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
BEGIN;
INSERT INTO my_table VALUES (6,2,2);
COMMIT;
```

   </td>
  </tr>
  <tr>
   <td>

```sql
INSERT INTO my_table VALUES (7,2,2);
COMMIT;
```

   </td>
   <td>
   </td>
  </tr>

</tbody>
</table>

Your PostgreSQL log should include interleaved output similar to the following:

```output
2022-12-08 14:11:24.190 EST [93243 15 639235e1.16c3b] [cloud1 datacenter1 rack1 node1] [127.0.0.1(49823) ysqlsh yugabyte yugabyte] LOG:  AUDIT: SESSION,6,1,MISC,BEGIN,,,begin;,<none>
2022-12-08 14:11:34.309 EST [93243 16 639235e1.16c3b] [cloud1 datacenter1 rack1 node1] [127.0.0.1(49823) ysqlsh yugabyte yugabyte] LOG:  AUDIT: SESSION,7,1,WRITE,INSERT,TABLE,public.my_table,"INSERT INTO my_table VALUES (5,2,2);",<none>
2022-12-08 14:11:38.294 EST [92937 8 639233f7.16b09] [cloud1 datacenter1 rack1 node1] [127.0.0.1(49633) ysqlsh yugabyte yugabyte] LOG:  AUDIT: SESSION,6,1,MISC,BEGIN,,,begin;,<none>
2022-12-08 14:11:42.976 EST [92937 9 639233f7.16b09] [cloud1 datacenter1 rack1 node1] [127.0.0.1(49633) ysqlsh yugabyte yugabyte] LOG:  AUDIT: SESSION,7,1,WRITE,INSERT,TABLE,public.my_table,"INSERT INTO my_table VALUES (6,2,2);",<none>
2022-12-08 14:11:46.596 EST [92937 10 639233f7.16b09] [cloud1 datacenter1 rack1 node1] [127.0.0.1(49633) ysqlsh yugabyte yugabyte] LOG:  AUDIT: SESSION,8,1,MISC,COMMIT,,,COMMIT;,<none>
2022-12-08 14:11:52.317 EST [93243 17 639235e1.16c3b] [cloud1 datacenter1 rack1 node1] [127.0.0.1(49823) ysqlsh yugabyte yugabyte] LOG:  AUDIT: SESSION,8,1,WRITE,INSERT,TABLE,public.my_table,"INSERT INTO my_table VALUES (7,2,2);",<none>
2022-12-08 14:11:54.374 EST [93243 18 639235e1.16c3b] [cloud1 datacenter1 rack1 node1] [127.0.0.1(49823) ysqlsh yugabyte yugabyte] LOG:  AUDIT: SESSION,9,1,MISC,COMMIT,,,commit;,<none>
```

Sorting by session identifier and timestamp, and including the node information for uniqueness in the cluster, you can group the transactions:

```output
cloud1 datacenter1 rack1 node1 639233f7.16b09 2022-12-08 14:11:38.294 SESSION,6,1,MISC,BEGIN,,,begin;,<none>
cloud1 datacenter1 rack1 node1 639233f7.16b09 2022-12-08 14:11:42.976 SESSION,7,1,WRITE,INSERT,TABLE,public.my_table,"INSERT INTO my_table VALUES (6,2,2);",<none>
cloud1 datacenter1 rack1 node1 639233f7.16b09 2022-12-08 14:11:46.596 SESSION,8,1,MISC,COMMIT,,,COMMIT;,<none>

cloud1 datacenter1 rack1 node1 639235e1.16c3b 2022-12-08 14:11:24.190 SESSION,6,1,MISC,BEGIN,,,begin;,<none>
cloud1 datacenter1 rack1 node1 639235e1.16c3b 2022-12-08 14:11:34.309 SESSION,7,1,WRITE,INSERT,TABLE,public.my_table,"INSERT INTO my_table VALUES (5,2,2);",<none>
cloud1 datacenter1 rack1 node1 639235e1.16c3b 2022-12-08 14:11:52.317 SESSION,8,1,WRITE,INSERT,TABLE,public.my_table,"INSERT INTO my_table VALUES (7,2,2);",<none>
cloud1 datacenter1 rack1 node1 639235e1.16c3b 2022-12-08 14:11:54.374 SESSION,9,1,MISC,COMMIT,,,commit;,<none>
```

## Learn more

- [Audit logging in YugabteDB](https://www.youtube.com/watch?v=ecYN9Z5_Hzc)
- [pgaudit GitHub project](https://github.com/pgaudit/pgaudit/)
- [PostgreSQL Extensions](../../../explore/ysql-language-features/pg-extensions/)

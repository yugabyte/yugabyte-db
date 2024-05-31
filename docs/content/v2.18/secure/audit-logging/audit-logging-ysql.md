---
title: Configure audit logging in YSQL
headerTitle: Configure audit logging in YSQL
description: Configure audit logging in YSQL.
menu:
  v2.18:
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

YugabyteDB YSQL uses PostgreSQL Audit Extension ([pgAudit](https://www.pgaudit.org/)) to provide detailed session and/or object audit logging via YugabyteDB YB-TServer logging.

The goal of the YSQL audit logging is to provide YugabyteDB users with capability to produce audit logs often required to comply with government, financial, or ISO certifications. An audit is an official inspection of an individual's or organization's accounts, typically by an independent body.

## Enable audit logging

To enable audit logging, first configure audit logging for the cluster. This is done in one of the following ways:

- At database startup.

    Use the [--ysql_pg_conf_csv](../../../reference/configuration/yb-tserver/#ysql-pg-conf-csv) YB-TServer flag.

    Database administrators can use `ysql_pg_conf_csv` to configure audit logging using [pgaudit flags](#customize-audit-logging).

    Provide the options as comma-separated values. Use double quotation marks to enclose any settings that include commas or single quotation marks. For example:

    ```sh
    --ysql_pg_conf_csv="log_line_prefix='%m [%p %l %c] %q[%C %R %Z %H] [%r %a %u %d] '","pgaudit.log='all, -misc'",pgaudit.log_parameter=on,pgaudit.log_relation=on,pgaudit.log_catalog=off,suppress_nonpg_logs=on
    ```

    These configuration values are set when the YugabyteDB cluster is created and therefore apply for all users and for every session.

- Per session.

    Use the [SET](../../../api/ysql/the-sql-language/statements/cmd_set/) command in a running session.

    The `SET` command essentially changes the run-time configuration parameters.

    For example, `SET pgaudit.log='DDL'`

    `SET` only affects the value used by the current session. For more information, see the [PostgreSQL documentation](https://www.postgresql.org/docs/11/sql-set.html).

### Create the extension

After configuring the YB-TServer and starting the cluster, create the `pgAudit` extension by executing the following statement in ysqlsh:

```sql
CREATE EXTENSION IF NOT EXISTS pgaudit;
```

You only need to run this statement on a single node, and it will apply across your cluster.

## Customize audit logging

You can customize YSQL audit logging using the `pgAudit` flags, as per the following table.

| Option | Description | Default |
| :----- | :----- | :------ |
| pgaudit.log | Specifies which classes of statements are logged by session audit logging, as follows:<ul><li>**READ**: SELECT and COPY when the source is a relation or a query.<li>**WRITE**: INSERT, UPDATE, DELETE, TRUNCATE, and COPY when the destination is a relation.<li>**FUNCTION**: Function calls and DO blocks.<li>**ROLE**: Statements related to roles and privileges: GRANT, REVOKE, CREATE/ALTER/DROP ROLE.<li>**DDL**: All DDL that is not included in the ROLE class.<li>**MISC**: Miscellaneous commands, such as DISCARD, FETCH, CHECKPOINT, VACUUM, SET.<li>**ALL**: Include all of the preceding options.</ul>Multiple classes can be provided using a comma-separated list and classes can be subtracted by prefacing the class with a minus (`-`) sign. | none |
| pgaudit.log_catalog | ON - Session logging would be enabled in the case for all relations in a statement that are in `pg_catalog`.<br>OFF - Disabling this setting reduces noise in the log from tools. | ON |
| pgaudit.log_client | ON - Log messages are to be visible to a client process such as psql. Helpful for debugging.<br>OFF - Reverse.<br>Note that `pgaudit.log_level` is only enabled when `pgaudit.log_client` is ON. | OFF |
| pgaudit.log_level | Values: DEBUG1 .. DEBUG5, INFO, NOTICE, WARNING, LOG.<br>Log level is used for log entries (ERROR, FATAL, and PANIC are not allowed). This setting is used for testing.<br>Note that `pgaudit.log_level` is only enabled when `pgaudit.log_client` is ON; otherwise the default is used. | LOG |
| pgaudit.log_parameter | ON - Audit logging includes the parameters that were passed with the statement. When parameters are present they are included in CSV format after the statement text. | OFF |
| pgaudit.log_relation | ON - Session audit logging creates separate log entries for each relation (TABLE, VIEW, and so on) referenced in a SELECT or DML statement. This is a shortcut for exhaustive logging without using object audit logging. | OFF |
| pgaudit.log_statement_once | ON - Specifies whether logging will include the statement text and parameters with the first log entry for a statement or sub-statement combination or with every entry. Disabling this setting results in less verbose logging but may make it more difficult to determine the statement that generated a log entry. | OFF |
| pgaudit.role | Specifies the master role to use for object audit logging. Multiple audit roles can be defined by granting them to the master role. This allows multiple groups to be in charge of different aspects of audit logging. | None |

## Example 1

Use the following steps to configure audit logging in a YugabyteDB cluster with bare minimum configurations.

### Enable audit logging

Start the YugabyteDB cluster with the following audit logging configuration:

```shell
--ysql_pg_conf_csv="pgaudit.log='DDL',pgaudit.log_level=notice,pgaudit.log_client=ON"
```

Alternatively, start ysqlsh and execute the following commands:

```shell
SET pgaudit.log='DDL';
SET pgaudit.log_client=ON;
SET pgaudit.log_level=notice;
```

### Load the pgAudit extension

To enable the `pgAudit` extension on the YugabyteDB cluster, create the `pgAudit` extension on any node as follows:

```sql
yugabyte=# CREATE EXTENSION IF NOT EXISTS pgaudit;
```

### Create a table and verify the log

As `pgaudit.log='DDL'` is configured, `CREATE TABLE` YSQL statements are logged and the corresponding log is shown in the YSQL client:

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

## Example 2

Use the following steps to configure advanced audit logging in a YugabyteDB cluster.

### Enable audit logging

Start the YugabyteDB cluster with the following audit logging configuration:

```shell
--ysql_pg_conf_csv="log_line_prefix='%m [%p %l %c] %q[%C %R %Z %H] [%r %a %u %d] '",pgaudit.log='all',pgaudit.log_parameter=on,pgaudit.log_relation=on,pgaudit.log_catalog=off,suppress_nonpg_logs=on
```

### Load the pgAudit extension

To enable the `pgAudit` extension on the YugabyteDB cluster, create the `pgAudit` extension on any node as follows:

```sql
yugabyte=# CREATE EXTENSION IF NOT EXISTS pgaudit;
yugabyte=# CREATE TABLE IF NOT EXISTS my_table ( h int, r int, v int, primary key(h,r));
```

### Generate a scenario with concurrent transactions

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
yugabyte=# BEGIN;
yugabyte=# INSERT INTO my_table VALUES (5,2,2);
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
yugabyte=# BEGIN;
yugabyte=# INSERT INTO my_table VALUES (6,2,2);
yugabyte=# COMMIT;
```

   </td>
  </tr>
  <tr>
   <td>

```sql
yugabyte=# INSERT INTO my_table VALUES (7,2,2);
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

```
cloud1 datacenter1 rack1 node1 639233f7.16b09 2022-12-08 14:11:38.294 SESSION,6,1,MISC,BEGIN,,,begin;,<none>
cloud1 datacenter1 rack1 node1 639233f7.16b09 2022-12-08 14:11:42.976 SESSION,7,1,WRITE,INSERT,TABLE,public.my_table,"INSERT INTO my_table VALUES (6,2,2);",<none>
cloud1 datacenter1 rack1 node1 639233f7.16b09 2022-12-08 14:11:46.596 SESSION,8,1,MISC,COMMIT,,,COMMIT;,<none>

cloud1 datacenter1 rack1 node1 639235e1.16c3b 2022-12-08 14:11:24.190 SESSION,6,1,MISC,BEGIN,,,begin;,<none>
cloud1 datacenter1 rack1 node1 639235e1.16c3b 2022-12-08 14:11:34.309 SESSION,7,1,WRITE,INSERT,TABLE,public.my_table,"INSERT INTO my_table VALUES (5,2,2);",<none>
cloud1 datacenter1 rack1 node1 639235e1.16c3b 2022-12-08 14:11:52.317 SESSION,8,1,WRITE,INSERT,TABLE,public.my_table,"INSERT INTO my_table VALUES (7,2,2);",<none>
cloud1 datacenter1 rack1 node1 639235e1.16c3b 2022-12-08 14:11:54.374 SESSION,9,1,MISC,COMMIT,,,commit;,<none>
```

## Read more

- [pgAudit GitHub project](https://github.com/pgaudit/pgaudit/)
- [PostgreSQL Extensions](../../../explore/ysql-language-features/pg-extensions/)

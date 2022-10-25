---
title: Configure audit logging in YSQL
headerTitle: Configure audit logging in YSQL
description: Configure audit logging in YSQL.
image: /images/section_icons/secure/authentication.png
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

YugabyteDB YSQL uses PostgreSQL Audit Extension ([pgAudit](https://www.pgaudit.org/)) to provide detailed session and/or object audit logging via YugabyteDB YB-TServer logging.

The goal of the YSQL audit logging is to provide YugabyteDB users with capability to produce audit logs often required to comply with government, financial, or ISO certifications. An audit is an official inspection of an individual's or organization's accounts, typically by an independent body.

## Enable audit logging

### Step 1. Enable audit logging on YB-TServer

This can be done in one of the following ways:

- Use the `--ysql_pg_conf_csv` YB-TServer flag.

    Database administrators can use `ysql_pg_conf_csv` to set appropriate values for `pgAudit` configuration.

    For example, `ysql_pg_conf_csv="pgaudit.log='DDL',pgaudit.log_level=notice"`

    These configuration values are set when the YugabyteDB cluster is created and hence are picked up for all users and for every session.

- Use the YugabyteDB `SET` command.

    An alternative is to use the YB `SET` command, which essentially changes the run-time configuration parameters.

    For example, `SET pgaudit.log='DDL'`

    `SET` only affects the value used by the current session. For more information, see the [PostgreSQL documentation](https://www.postgresql.org/docs/11/sql-set.html).

### Step 2. Load the `pgAudit` extension

Enable audit logging in YugabyteDB clusters by creating the `pgAudit` extension. Executing the following statement in a YSQL shell enables Audit logging:

```sql
CREATE EXTENSION IF NOT EXISTS pgaudit;
```

## Customize audit logging

You can further customize YSQL audit logging by configuring the `pgAudit` flags, as per the following table.

| Option | Description | Default |
| :----- | :----- | :------ |
| pgaudit.log | Specifies which classes of statements are logged by session audit logging, as follows:<ul><li>**READ**: SELECT and COPY when the source is a relation or a query.<li>**WRITE**: INSERT, UPDATE, DELETE, TRUNCATE, and COPY when the destination is a relation.<li>**FUNCTION**: Function calls and DO blocks.<li>**ROLE**: Statements related to roles and privileges: GRANT, REVOKE, CREATE/ALTER/DROP ROLE.<li>**DDL**: All DDL that is not included in the ROLE class.<li>**MISC**: Miscellaneous commands, such as DISCARD, FETCH, CHECKPOINT, VACUUM, SET.<li>**MISC_SET**: Miscellaneous SET commands, such as SET ROLE.<li>**ALL**: Include all of the preceding options.</ul>Multiple classes can be provided using a comma-separated list and classes can be subtracted by prefacing the class with a minus (`-`) sign. | none |
| pgaudit.log_catalog | ON - Session logging would be enabled in the case for all relations in a statement that are in `pg_catalog`.<br>OFF - Disabling this setting reduces noise in the log from tools. | ON |
| pgaudit.log_client | ON - Log messages are to be visible to a client process such as psql. Helpful for debugging.<br>OFF - Reverse.<br>Note that `pgaudit.log_level` is only enabled when `pgaudit.log_client` is ON. | OFF |
| pgaudit.log_level | Values: DEBUG1 .. DEBUG5, INFO, NOTICE, WARNING, LOG.<br>Log level is used for log entries (ERROR, FATAL, and PANIC are not allowed). This setting is used for testing.<br>Note that `pgaudit.log_level` is only enabled when `pgaudit.log_client` is ON; otherwise the default is used. | LOG |
| pgaudit.log_parameter | ON - Audit logging includes the parameters that were passed with the statement. When parameters are present they are included in CSV format after the statement text. | OFF |
| pgaudit.log_relation | ON - Session audit logging creates separate log entries for each relation (TABLE, VIEW, and so on) referenced in a SELECT or DML statement. This is a shortcut for exhaustive logging without using object audit logging. | OFF |
| pgaudit.log_statement_once | ON - Specifies whether logging will include the statement text and parameters with the first log entry for a statement or sub-statement combination or with every entry. Disabling this setting results in less verbose logging but may make it more difficult to determine the statement that generated a log entry. | OFF |
| pgaudit.role | Specifies the master role to use for object audit logging. Multiple audit roles can be defined by granting them to the master role. This allows multiple groups to be in charge of different aspects of audit logging. | None |

## Example

Use these steps to configure audit logging in a YugabyteDB cluster with bare minimum configurations.

### 1. Enable audit logging

Start the YugabyteDB Cluster with the following Audit logging configuration:

```shell
--ysql_pg_conf_csv="pgaudit.log='DDL',pgaudit.log_level=notice,pgaudit.log_client=ON"
```

Alternatively, open the YSQL shell and execute the following commands:

```shell
SET pgaudit.log='DDL';
SET pgaudit.log_client=ON;
SET pgaudit.log_level=notice;
```

### 2. Load `pgAudit` extension

Open the YSQL shell (ysqlsh), specifying the `yugabyte` user, as follows:

```shell
$ ./ysqlsh -U yugabyte -W
```

When prompted, enter the password for the `yugabyte` user.

You should be able to login and see an output similar to the following:

```output
ysqlsh (11.2-YB-2.5.0.0-b0)
Type "help" for help.

yugabyte=#
```

To enable the `pgAudit` extension on the YugabyteDB cluster, connect to the database by using the following:

```shell
yugabyte=> \c yugabyte yugabyte;
```

You are now connected to the database `yugabyte` as user `yugabyte`.

Finally, create the `pgAudit` extension as follows:

```sql
CREATE EXTENSION IF NOT EXISTS pgaudit;
```

### 3. Create a table and verify log

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

## Read more

- [pgAudit GitHub project](https://github.com/pgaudit/pgaudit/)
- [PostgreSQL Extensions](../../../explore/ysql-language-features/pg-extensions/)

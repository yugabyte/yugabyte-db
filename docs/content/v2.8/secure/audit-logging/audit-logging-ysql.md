---
title: Configure Audit Logging in YSQL
headerTitle: Configure Audit Logging in YSQL
linkTitle: Configure Audit Logging in YSQL
description: Configure Audit Logging in YSQL.
headcontent: Configure Audit Logging in YSQL.
image: /images/section_icons/secure/authentication.png
menu:
  v2.8:
    name: Configure Audit Logging
    identifier: enable-audit-logging-1-ysql
    parent: audit-logging
    weight: 755
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/preview/secure/audit-logging/audit-logging-ysql" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="/preview/secure/audit-logging/audit-logging-ycql" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

YugabyteDB YSQL uses PostgreSQL Audit Extension (`pgAudit`) to provide detailed session and/or object audit logging via YugabyteDB TServer logging.

The goal of the YSQL audit logging is to provide YugabyteDB users with capability to produce audit logs often required to comply with government, financial, or ISO certifications. An audit is an official inspection of an individual’s or organization’s accounts, typically by an independent body.


## Enabling Audit Logging


### Step 1. Enable audit logging on YB-TServer

This can be done in one of the following ways:

##### Option A: Using `--ysql_pg_conf` TServer flag

Database administrators can leverage `ysql_pg_conf` to set appropriate values for `pgAudit` configuration.

For example, `ysql_pg_conf="pgaudit.log='DDL',pgaudit.log_level=notice"`

These configuration values are set when the YugabyteDB cluster is created and hence are picked up for all users and for every session.


##### Option B: Using YugabyteDB `SET` command

An alternative suggestion is to use the YB `SET` command, which essentially changes the run-time configuration parameters.

For example, `SET pgaudit.log='DDL'`

`SET` only affects the value used by the current session. For more information, see the [PostgreSQL documentation](https://www.postgresql.org/docs/11/sql-set.html).


### Step 2. Load the `pgAudit` extension

Enable audit logging in YugabyteDB clusters by creating the `pgAudit` extension. Executing the following statement in a YSQL shell enables Audit logging:

```sql
CREATE EXTENSION IF NOT EXISTS pgaudit;
```


## Customizing Audit Logging

YSQL audit logging can be further customized by configuring the `pgAudit` flags as per the following table.

<table>
  <tr>
   <td><strong>Option</strong>
   </td>
   <td><strong>Values notes</strong>
   </td>
  </tr>
  <tr>
   <td><code>pgaudit.log</code>
   </td>
   <td>Specifies which classes of statements are to be logged by <strong>session audit logging</strong>.
<ul>


<li><strong><code>READ</code></strong>: <code>SELECT</code> and <code>COPY</code> when the source is a relation or a query.

<li><strong><code>WRITE</code></strong>: <code>INSERT</code>, <code>UPDATE</code>, <code>DELETE</code>, <code>TRUNCATE</code>, and <code>COPY</code> when the destination is a relation.

<li><strong><code>FUNCTION</code></strong>: Function calls and <code>DO</code> blocks.

<li><strong><code>ROLE</code></strong>: Statements related to roles and privileges: <code>GRANT</code>, <code>REVOKE</code>, <code>CREATE/ALTER/DROP ROLE</code>.

<li><strong><code>DDL</code></strong>: All <code>DDL</code> that is not included in the <code>ROLE</code> class.

<li><strong><code>MISC</code></strong>: Miscellaneous commands, such as <code>DISCARD, FETCH, CHECKPOINT, VACUUM, SET</code>.

<li><strong><code>MISC_SET</code></strong>: Miscellaneous <code>SET</code> commands, such as <code>SET ROLE</code>.

<li><strong><code>ALL</code></strong>: Include all of the preceding options.

Multiple classes can be provided using a comma-separated list and classes can be subtracted by prefacing the class with a `-` (minus) sign.

</li>
</ul>

The default is none.
   </td>
  </tr>
  <tr>
   <td><code>pgaudit.log_catalog</code>
   </td>
   <td><strong><code>ON</code></strong>: <strong>Session logging</strong> would be enabled in the case for all relations in a statement that are in pg_catalog.
<strong><code>OFF</code></strong>: Disabling this setting will reduce noise in the log from tools.<p>
The default is <strong><code>ON</code></strong>.
   </td>
  </tr>
  <tr>
   <td><code>pgaudit.log_client</code>
   </td>
   <td><strong><code>ON</code></strong>: Log messages are to be visible to a client process such as psql. Useful for debugging.
<strong><code>OFF</code></strong>: Reverse.
Note that `pgaudit.log_level` is only enabled when pgaudit.log_client is <strong><code>ON</code></strong>.<p>
The default is <strong><code>OFF</code></strong>.
   </td>
  </tr>
  <tr>
   <td><code>pgaudit.log_level</code>
   </td>
   <td>Values: <strong><code>DEBUG1 .. DEBUG5, INFO, NOTICE, WARNING, LOG</code></strong>.
Log level to be used for log entries (<code>ERROR</code>, <code>FATAL</code>, and <code>PANIC</code> are not allowed). This setting is used for testing.

<p>
Note that <code>pgaudit.log_level</code> is only enabled when pgaudit.log_client is <strong><code>ON</code></strong>; otherwise the default will be used.<br><br>
The default is <strong><code>LOG</code></strong>.
   </td>
  </tr>
  <tr>
   <td><code>pgaudit.log_parameter</code>
   </td>
   <td><strong><code>ON</code></strong>: Audit logging includes the parameters that were passed with the statement. When parameters are present they will be included in CSV format after the statement text.
<p>
The default is <strong><code>OFF</code></strong>.
   </td>
  </tr>
  <tr>
   <td><code>pgaudit.log_relation</code>
   </td>
   <td><strong><code>ON</code></strong>: Session audit logging creates separate log entries for each relation (<code>TABLE</code>, <code>VIEW</code>, etc.) referenced in a <code>SELECT</code> or <code>DML</code> statement. This is a useful shortcut for exhaustive logging without using <strong>object audit logging</strong>.
<p>
The default is <strong><code>OFF</code></strong>.
   </td>
  </tr>
  <tr>
   <td><code>pgaudit.log_statement_once</code>
   </td>
   <td><strong><code>ON</code></strong>: Specifies whether logging will include the statement text and parameters with the first log entry for a statement/substatement combination or with every entry. Disabling this setting will result in less verbose logging but may make it more difficult to determine the statement that generated a log entry.
<p>
The default is <strong><code>OFF</code></strong>.
   </td>
  </tr>
  <tr>
   <td><code>pgaudit.role</code>
   </td>
   <td>Specifies the master role to use for <strong>object audit logging</strong>. Multiple audit roles can be defined by granting them to the master role. This allows multiple groups to be in charge of different aspects of audit logging.
<p>
There is no default.
   </td>
  </tr>
</table>

## Examples

Use these steps to configure audit logging in a YugabyteDB cluster with bare minimum configurations.


### 1. Enable audit logging

Start the YugabyteDB Cluster with the following Audit logging configuration:

```shell
--ysql_pg_conf="pgaudit.log='DDL',pgaudit.log_level=notice,pgaudit.log_client=ON"
```

Alternatively, open the YSQL shell and execute the following commands:


```shell
SET pgaudit.log='DDL';
SET pgaudit.log_client=ON;
SET pgaudit.log_level=notice;
```


### 2. Load `pgAudit` extension

Open the YSQL shell (ysqlsh), specifying the `yugabyte` user and prompting for the password, as follows:

```shell
$ ./ysqlsh -U yugabyte -W
```

When prompted for the password, enter the yugabyte password.

You should be able to log in and see an output similar to the following:


    ysqlsh (11.2-YB-2.5.0.0-b0)
    Type "help" for help.

    yugabyte=#

To enable the `pgAudit` extension on the YugabyteDB cluster, connect to the database by using the following:

```shell
yugabyte=> \c yugabyte yugabyte;
```

You are now connected to database called yugabyte as user yugabyte.

Finally, create the `pgAudit` extension as follows:

```sql
CREATE EXTENSION IF NOT EXISTS pgaudit;
```

### 3. Create a table and verify log

Since `pgaudit.log='DDL'` is configured, `CREATE TABLE` YSQL statements are logged and the corresponding log is shown in the YSQL client:


```sql
CREATE TABLE employees (empno int, ename text, address text,
  salary int, account_number text);
```

```
NOTICE:  AUDIT: SESSION,2,1,DDL,CREATE TABLE,TABLE,public.employees,
"create table employees ( empno int, ename text, address text, salary int,
account_number text );",<not logged>
CREATE TABLE
```

Notice that audit logs are generated for DDL statements.

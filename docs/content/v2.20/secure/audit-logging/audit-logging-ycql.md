---
title: Configure audit logging in YCQL
headerTitle: Configure audit logging in YCQL
description: Configure audit logging in YCQL.
menu:
  v2.20:
    name: Configure audit logging
    identifier: enable-audit-logging-2-ycql
    parent: audit-logging
    weight: 755
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../audit-logging-ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../audit-logging-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

Audit logging can be used to record information about YCQL statements or events (such as login events) and log the records on a per-node basis into the YB-Tserver logs. Audit logging can be enabled on YugabyteDB cluster by setting the `ycql_enable_audit_log` TServer flag to `true`. By default, each TServer records all login events and YCQL commands issued to the server.

Audit record is logged before an operation attempts to be executed, and failures are audited as well. If an operation fails to execute, both operation execution and failure are logged. However, an error that happens during parsing or analysis of YCQL statement results only in an error audit record to be logged.

YCQL audit logging can be further customized using additional YB-TServer flags.

## Enable audit logging

Audit logging for YCQL can be enabled by passing the `--ycql_enable_audit_log` flag to `yb-tserver`. The command to start the `yb-tserver` would look as follows:

```sh
$ yb-tserver <options> --ycql_enable_audit_log=true
```

## Configure audit logging

Statements or events are recorded if they match _all_ [audit filters](#audit-filters). That is, only the configured categories in the configured keyspaces by the configured users are recorded.

For the `included` flags, the default value (empty) means everything is included, while for the `excluded` flags the default value (empty) means nothing is excluded. By default everything is logged except events in system keyspaces.

If both the inclusion and exclusion flags are set for the same dimension (for example, users) then statements or events are recorded only if both match; that is, if they are in the set-difference between included entries and excluded entries. So that is allowed although it is redundant: the same semantics can be achieved by setting only the inclusion flag to the resulting set-difference.

The `ycql_audit_log_level` determines the log file where the audit records are written (that is, `yb-tserver.INFO`, `yb-tserver.WARNING`, or `yb-tserver.ERROR`).

Only `ERROR`-level logs are immediately flushed to disk, lower levels might be buffered.

## Audit filters

### Objects being audited

YB-TServer flags can be configured to determine which statements and events should be logged, audit logging can be configured along three different dimensions: categories (statement or event_)_ , users, and keyspaces.

Each can be configured either by inclusion (listing all statement categories, users, or keyspaces to be audited) or by exclusion of CQL commands (listing all statement categories, user, or keyspaces to be excluded from auditing).

The available flags are described in the following table:

<table>
  <tr>
   <td><strong>Flag</strong></td>
   <td><strong>Valid Values</strong></td>
   <td><strong>Description</strong></td>
   <td><strong>Default Value</strong></td>
  </tr>
  <tr>
   <td><code>ycql_enable_audit_log</code></td>
   <td><code>true</code>/<code>false</code></td>
   <td>Enable YCQL audit</td>
   <td><code>false</code></td>
  </tr>
  <tr>
   <td><code>ycql_audit_included_categories</code></td>
   <td rowspan="2" >Comma-separated list of statement categories.</td>
   <td>Categories to audit</td>
   <td>empty</td>
  </tr>
  <tr>
   <td><code>ycql_audit_excluded_categories</code></td>
   <td>Categories to exclude</td>
   <td>empty</td>
  </tr>
  <tr>
   <td><code>ycql_audit_included_users</code></td>
   <td rowspan="2" >Comma-separated list of users.</td>
   <td>Users to audit</td>
   <td>empty</td>
  </tr>
  <tr>
   <td><code>ycql_audit_excluded_users</code></td>
   <td>Users to exclude</td>
   <td>empty</td>
  </tr>
  <tr>
   <td><code>ycql_audit_included_keyspaces</code></td>
   <td rowspan="2" >Comma-separated list of keyspaces.</td>
   <td>keyspaces to audit</td>
   <td>empty</td>
  </tr>
  <tr>
   <td><code>ycql_audit_excluded_keyspaces</code></td>
   <td>keyspaces to exclude</td>
   <td><code>system,system_schema,system_virtual_schema,system_auth</code></td>
  </tr>
  <tr>
   <td><code>ycql_audit_log_level</code></td>
   <td><code>INFO</code>, <code>WARNING</code>, or <code>ERROR</code>.</td>
   <td>Severity level at which an audit is logged.</td>
   <td><code>ERROR</code></td>
  </tr>
</table>

All the preceding flags are `runtime` flags, so they can be set without requiring `yb-tserver` restart.

### Statements being audited

The valid statement categories are described in the following table.

<table>
  <tr>
   <td><strong>Audit Category</strong>
   </td>
   <td><strong>Covered YCQL statements or wire-protocol events</strong>
   </td>
  </tr>
  <tr>
   <td><code>QUERY</code>
   </td>
   <td><code>SELECT</code>
   </td>
  </tr>
  <tr>
   <td><code>DML</code>
   </td>
   <td><code>INSERT, UPDATE, DELETE, BEGIN TRANSACTION, </code>and batch statements.
   </td>
  </tr>
  <tr>
   <td><code>DDL</code>
   </td>
   <td><code>TRUNCATE, CREATE/ALTER/DROP KEYSPACE/TABLE/INDEX/TYPE </code>
   </td>
  </tr>
  <tr>
   <td><code>DCL</code>
   </td>
   <td><code>LIST USERS/ROLES/PERMISSIONS, GRANT, REVOKE, CREATE/ALTER/DROP ROLE</code>
   </td>
  </tr>
  <tr>
   <td><code>AUTH</code>
   </td>
   <td>Login error, login attempt, login success
   </td>
  </tr>
  <tr>
   <td><code>PREPARE</code>
   </td>
   <td>Prepared statement
   </td>
  </tr>
  <tr>
   <td><code>ERROR</code>
   </td>
   <td>Request failure
   </td>
  </tr>
  <tr>
   <td><code>OTHER</code>
   </td>
   <td><code>USE &lt;keyspace>, EXPLAIN</code>
   </td>
  </tr>
</table>

## Output format

Log record for a `CREATE TABLE` statement executed by user `john`, on keyspace `prod`:

```output
E0920 09:07:30.679694 10725 audit_logger.cc:552] AUDIT: user:john|
host:172.151.36.146:9042|source:10.9.80.22|port:56480|timestamp:1600592850679|
type:CREATE_TABLE|category:DDL|ks:prod|scope:test_table|operation:create table
test_table(k int primary key, v int);
```

Each audit log record has the following components:

<table>
  <tr>
   <td><strong>Field</strong>

   </td>
   <td><strong>Notes</strong>

   </td>
  </tr>
  <tr>
   <td><code>user</code></td>
   <td>User name (if available)</td>
  </tr>
  <tr>
   <td><code>host</code>
   </td>
   <td>IP of the node where the command is being executed
   </td>
  </tr>
  <tr>
   <td><code>source</code>
   </td>
   <td>IP address from where the request initiated
   </td>
  </tr>
  <tr>
   <td><code>port</code>
   </td>
   <td>Port number from where the request initiated
   </td>
  </tr>
  <tr>
   <td><code>timestamp</code>
   </td>
   <td>Unix timestamp (in milliseconds)
   </td>
  </tr>
  <tr>
   <td><code>type</code>
   </td>
   <td>Type of the request (`SELECT`, `INSERT`, etc.,)
   </td>
  </tr>
  <tr>
   <td><code>category</code>
   </td>
   <td>Category of the request (`DDL`, `DML`, etc.,)
   </td>
  </tr>
  <tr>
   <td><code>ks</code>
   </td>
   <td>Keyspace on which request is targeted to be executed (if applicable)
   </td>
  </tr>
  <tr>
   <td><code>scope</code>
   </td>
   <td>Target of the current operation, such as the table, user, type, or keyspace name for corresponding `CREATE`, `ALTER`, or `DROP` commands.
   </td>
  </tr>
  <tr>
   <td><code>operation</code>
   </td>
   <td>The YCQL command being executed.
   </td>
  </tr>
</table>

## Configuration examples

This section shows some examples of how to configure audit logging.

##### Log auth events only

```output
ycql_enable_audit_log=true
ycql_audit_included_categories=AUTH
```

##### Log everything except SELECTs and DMLs

```output
ycql_enable_audit_log=true
ycql_audit_excluded_categories=QUERY,DML
```

##### Log just DDLs on keyspaces `ks1` by `user1`

```output
ycql_enable_audit_log=true
ycql_audit_included_categories=DDL
ycql_audit_included_keyspace=ks1
ycql_audit_included_users=user1
```

##### Log DCLs by everyone except user `dbadmin`

```output
ycql_enable_audit_log=true
ycql_audit_included_categories=DCL
ycql_audit_excluded_users=dbadmin
```

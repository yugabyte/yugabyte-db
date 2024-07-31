---
title: Trace executed statements in YSQL
headerTitle: Manually trace executed statements in YSQL
description: Tracing executed statements in YSQL.
menu:
  v2.20:
    name: Trace statements
    identifier: trace-statements-ysql
    parent: audit-logging
    weight: 555
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../trace-statements-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

To trace executed statements in a session, you can use session identifiers. Session identifiers can be used to filter PostgreSQL log files for statements executed in a specific session and are unique in a YB-TServer node. A session identifier is a combination of process start time and PostgreSQL process ID (PID) and is output to the logs in hexadecimal format.

Note that in a YugabyteDB cluster with multiple nodes, session identifier is not guaranteed to be unique; both process start time and the PostgreSQL PID can be the same across different nodes. Be sure to connect to the node where the statements were executed.

## Set logging options

To log the appropriate session information, you need to set the following configuration flags for your YB-TServers:

- Set [ysql_log_statement](../../../reference/configuration/yb-tserver/#ysql-log-statement) YB-TServer configuration flag to `all` to turn on statement logging in the PostgreSQL logs.
- Set the `log_line_prefix` PostgreSQL server option to log timestamp, PostgreSQL PID, and session identifier.

    YugabyteDB includes additional logging options so that you can record distributed location information. For example:

    ```sh
    --ysql_pg_conf_csv="log_line_prefix='%m [%p %l %c] %q[%C %R %Z %H] [%r%a %u %d] '"
    ```

    The parameters are similar to that of PostgreSQL, with the addition of H, C, R, and Z to add host, cloud, region, and zone information relevant to distributed systems. You can also add `%U` and `%N` to add the cluster UUID, and node and cluster name respectively.

    For information on setting server options in YugabyteDB, refer to [PostgreSQL server options](../../../reference/configuration/yb-tserver/#postgresql-server-options).

## Review logs

Session information is written to the PostgreSQL logs, located in the YugabyteDB base folder in the `yb-data/tserver/logs` directory. For information on inspecting logs, refer to [Inspect YugabyteDB logs](../../../troubleshoot/nodes/check-logs/).

## Example session

Create a local cluster and configure `ysql_log_statement` to log all statements, and `log_line_prefix` to log timestamp, PostgreSQL PID, and session identifier as follows:

```sh
./bin/yugabyted start --tserver_flags="ysql_log_statement=all,ysql_pg_conf_csv=\"log_line_prefix='timestamp: %m pid: %p session: %c '\""
```

For local clusters created using yugabyted, `postgresql` logs are located in `~/var/data/yb-data/tserver/logs`.

Connect to the cluster using ysqlsh as follows:

```sh
./bin/ysqlsh
```

```output
ysqlsh (11.2-YB-2.15.2.1-b0)
Type "help" for help.

yugabyte=# 
```

### Trace statement execution

Execute the following commands:

```sql
CREATE TABLE my_table ( h int, r int, v int, primary key(h,r));
```

```output
CREATE TABLE
```

```sql
INSERT INTO my_table VALUES (1, 1, 1);
```

```output
INSERT 0 1
```

Your PostgreSQL log should include output similar to the following:

```output
timestamp: 2022-10-24 16:49:42.825 UTC --pid: 1930 session: 6356c208.78a LOG:  statement: CREATE TABLE my_table ( h int, r int, v int, primary key(h,r));
timestamp: 2022-10-24 16:51:01.258 UTC --pid: 1930 session: 6356c208.78a LOG:  statement: INSERT INTO my_table VALUES (1, 1, 1);
```

### Trace an explicit transaction

Start an explicit transaction as follows:

```sql
BEGIN;
```

```output
BEGIN
```

```sql
INSERT INTO my_table VALUES (2,2,2);
```

```output
INSERT 0 1
```

```sql
DELETE FROM my_table WHERE h = 1;
```

```output
DELETE 1
```

```sql
COMMIT;
```

```output
COMMIT
```

Your PostgreSQL log should include output similar to the following:

```output
timestamp: 2022-10-24 16:56:56.269 UTC --pid: 1930 session: 6356c208.78a LOG:  statement: BEGIN;
timestamp: 2022-10-24 16:57:05.410 UTC --pid: 1930 session: 6356c208.78a LOG:  statement: INSERT INTO my_table VALUES (2,2,2);
timestamp: 2022-10-24 16:57:25.015 UTC --pid: 1930 session: 6356c208.78a LOG:  statement: DELETE FROM my_table WHERE h = 1;
timestamp: 2022-10-24 16:57:27.595 UTC --pid: 1930 session: 6356c208.78a LOG:  statement: COMMIT;
```

### Trace concurrent transactions

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
COMMIT;
```

   </td>
   <td>
   </td>
  </tr>

</tbody>
</table>

Your PostgreSQL log should include output similar to the following:

```output
timestamp: 2022-10-24 17:04:09.007 UTC --pid: 1930 session: 6356c208.78a LOG:  statement: BEGIN;
timestamp: 2022-10-24 17:05:10.647 UTC --pid: 1930 session: 6356c208.78a LOG:  statement: INSERT INTO my_table VALUES (5,2,2);
timestamp: 2022-10-24 17:05:15.042 UTC --pid: 2343 session: 6356c4a4.927 LOG:  statement: BEGIN;
timestamp: 2022-10-24 17:05:19.227 UTC --pid: 2343 session: 6356c4a4.927 LOG:  statement: INSERT INTO my_table VALUES (6,2,2);
timestamp: 2022-10-24 17:05:22.288 UTC --pid: 2343 session: 6356c4a4.927 LOG:  statement: COMMIT;
timestamp: 2022-10-24 17:05:25.404 UTC --pid: 1930 session: 6356c208.78a LOG:  statement: COMMIT;
```

## Next steps

Use `pgaudit` to enable logging for specific databases, tables, or specific sets of operations. See [Configure audit logging in YSQL](../audit-logging-ysql).

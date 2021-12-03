---
title: Build a C application that uses YSQL
headerTitle: Build a C application
linkTitle: C
description: Build a sample C application with libpq.
aliases:
  - /develop/client-drivers/c/
  - /latest/develop/client-drivers/c/
  - /latest/quick-start/build-apps/c/
menu:
  latest:
    identifier: build-apps-c-1-ysql
    parent: build-apps
    weight: 557
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

</ul>

{{< tip title="Yugabyte Cloud requires SSL" >}}

Are you using Yugabyte Cloud? Install the [prerequisites](#prerequisites), then go to the [Use C with SSL](#use-c-with-ssl) section.

{{</ tip >}}

## Prerequisites

The tutorial assumes that you have:

- installed YugabyteDB and created a universe. If not, follow the steps in [Quick start](../../../../quick-start/).
- have a 32-bit (x86) or 64-bit (x64) architecture machine.
- have gcc 4.1.2 or later, clang 3.4 or later installed.

## Install the libpq C driver

The `libpq` C driver is included in the YugabyteDB installation. You can use it by setting the `LD_LIBRARY_PATH` as follows:

```sh
$ export LD_LIBRARY_PATH=<yugabyte-install-dir>/postgres/lib
```

Alternatively, you can download the PostgreSQL binaries or build the driver from source as documented [here](https://www.postgresql.org/download/).

## Create a sample C application

### Sample C code

Create a file `ybsql_hello_world.c` and copy the contents below:

```cpp
#include <stdio.h>
#include <stdlib.h>
#include "libpq-fe.h"

int
main(int argc, char **argv)
{
  const char *conninfo;
  PGconn     *conn;
  PGresult   *res;
  int         nFields;
  int         i, j;

  /* connection string */
  conninfo = "host=127.0.0.1 port=5433 dbname=yugabyte user=yugabyte password=yugabyte";

  /* Make a connection to the database */
  conn = PQconnectdb(conninfo);

  /* Check to see that the backend connection was successfully made */
  if (PQstatus(conn) != CONNECTION_OK)
  {
      fprintf(stderr, "Connection to database failed: %s",
        PQerrorMessage(conn));
      PQfinish(conn);
      exit(1);
  }

  /* Create table */
  res = PQexec(conn, "CREATE TABLE employee (id int PRIMARY KEY, \
                                             name varchar, age int, \
                                             language varchar)");

  if (PQresultStatus(res) != PGRES_COMMAND_OK)
  {
      fprintf(stderr, "CREATE TABLE failed: %s", PQerrorMessage(conn));
      PQclear(res);
      PQfinish(conn);
      exit(1);
  }
  PQclear(res);
  printf("Created table employee\n");

  /* Insert a row */
  res = PQexec(conn, "INSERT INTO employee (id, name, age, language) \
                      VALUES (1, 'John', 35, 'C')");

  if (PQresultStatus(res) != PGRES_COMMAND_OK)
  {
      fprintf(stderr, "INSERT failed: %s", PQerrorMessage(conn));
      PQclear(res);
      PQfinish(conn);
      exit(1);
  }
  PQclear(res);
  printf("Inserted data (1, 'John', 35, 'C')\n");


  /* Query the row */
  res = PQexec(conn, "SELECT name, age, language FROM employee WHERE id = 1");
  if (PQresultStatus(res) != PGRES_TUPLES_OK)
  {
      fprintf(stderr, "SELECT failed: %s", PQerrorMessage(conn));
      PQclear(res);
      PQfinish(conn);
      exit(1);
  }

  /* print out the rows */
  nFields = PQnfields(res);
  for (i = 0; i < PQntuples(res); i++)
  {
      printf("Query returned: ");
      for (j = 0; j < nFields; j++)
        printf("%s ", PQgetvalue(res, i, j));
      printf("\n");
  }
  PQclear(res);

  /* close the connection to the database and cleanup */
  PQfinish(conn);

  return 0;
}
```

### Run the application

You can compile the file using `gcc` or `clang`.
To compile the application with`gcc`, run the following command:

```sh
$ gcc ybsql_hello_world.c -lpq -I<yugabyte-install-dir>/postgres/include -o ybsql_hello_world
```

Run with:

```sh
$ ./ybsql_hello_world
```

You should see the following output:

```output
Created table employee
Inserted data (1, 'John', 35, 'C')
Query returned: John 35 C
```

## Use C with SSL

The client driver supports several SSL modes, as follows:

| SSL mode | Client driver behavior |
| :------- | :--------------------- |
| disable | Supported |
| allow | Supported |
| prefer | Supported (default) |
| require | Supported |
| verify-ca | Supported |
| verify-full | Supported |

{{< note title="SSL mode support" >}}

The default SSL mode for libpq is `prefer`. In the `require` SSL mode, the root CA certificate is not required to be configured.

To enable `verify-ca` and `verify-full`, you configure the path to the CA in the connection string using the `sslrootcert` parameter. The default path is `~/.postgresql/root.crt`. If the root certificate is in a different file, specify it in the sslrootcert parameter. For example:

```c
conninfo = "host=331be4e2-fe24-3d2f-84f0-6d0d49ed422b.cloudportal.yugabyte.com port=5433 dbname=yugabyte user=<username> password=<password> sslmode=verify-full sslrootcert=/Users/yugabyte/Downloads/root.crt"
```

**Always use verify-full mode if you're using a public CA**. The difference between `verify-ca` and `verify-full` depends on the policy of the root CA. If a you're using a public CA, verify-ca allows connections to a server that somebody else may have registered with the CA. If you're using a local CA, or even a self-signed certificate, using verify-ca often provides enough protection, but using verify-full is always the best security practice.

{{< /note >}}

### Create a sample C application with SSL

Create a file `ybsql_hello_world_ssl.c` and copy the contents below:

```c
#include <stdio.h>
#include <stdlib.h>
#include "libpq-fe.h"

int
main(int argc, char **argv)
{
  const char *conninfo;
  PGconn     *conn;
  PGresult   *res;
  int         nFields;
  int         i, j;

  /* connection string */
  conninfo = "host=331bd8e2-fe24-4d2f-84f0-6d0d49edc22b.cloudportal.yugabyte.com port=5433 dbname=yugabyte user=<username> password=<password> sslmode=verify-full";

  /* Make a connection to the database */
  conn = PQconnectdb(conninfo);

  /* Check to see that the backend connection was successfully made */
  if (PQstatus(conn) != CONNECTION_OK)
  {
      fprintf(stderr, "Connection to database failed: %s",
        PQerrorMessage(conn));
      PQfinish(conn);
      exit(1);
  }

  /* Create table */
  res = PQexec(conn, "CREATE TABLE employee (id int PRIMARY KEY, \
                                             name varchar, age int, \
                                             language varchar)");

  if (PQresultStatus(res) != PGRES_COMMAND_OK)
  {
      fprintf(stderr, "CREATE TABLE failed: %s", PQerrorMessage(conn));
      PQclear(res);
      PQfinish(conn);
      exit(1);
  }
  PQclear(res);
  printf("Created table employee\n");

  /* Insert a row */
  res = PQexec(conn, "INSERT INTO employee (id, name, age, language) \
                      VALUES (1, 'John', 35, 'C')");

  if (PQresultStatus(res) != PGRES_COMMAND_OK)
  {
      fprintf(stderr, "INSERT failed: %s", PQerrorMessage(conn));
      PQclear(res);
      PQfinish(conn);
      exit(1);
  }
  PQclear(res);
  printf("Inserted data (1, 'John', 35, 'C')\n");

  /* Query the row */
  res = PQexec(conn, "SELECT name, age, language FROM employee WHERE id = 1");
  if (PQresultStatus(res) != PGRES_TUPLES_OK)
  {
      fprintf(stderr, "SELECT failed: %s", PQerrorMessage(conn));
      PQclear(res);
      PQfinish(conn);
      exit(1);
  }

  /* print out the rows */
  nFields = PQnfields(res);
  for (i = 0; i < PQntuples(res); i++)
  {
      printf("Query returned: ");
      for (j = 0; j < nFields; j++)
        printf("%s ", PQgetvalue(res, i, j));
      printf("\n");
  }
  PQclear(res);

  /* close the connection to the database and cleanup */
  PQfinish(conn);

  return 0;
}
```

### Run the C SSL application

You can compile the file using `gcc` or `clang`.
To compile the application with `clang`, run the following command:

> **FIXME** adjust the path once we get this working

```sh
$ clang yb_hello_world_ssl.c -lpq -I ~/devel/yb-versions/2.9.1.0-b136/postgres/include -o yb_hello_world_ssl
```

Run with:

```sh
$ ./ybsql_hello_world
```

You should see the following output:

```output
Created table employee
Inserted data (1, 'John', 35, 'C')
Query returned: John 35 C
```

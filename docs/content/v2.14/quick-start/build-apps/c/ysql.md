---
title: Build a C application that uses YSQL
headerTitle: Build a C application
linkTitle: C
description: Build a sample C application with libpq.
menu:
  v2.14:
    identifier: build-apps-c-1-ysql
    parent: build-apps
    weight: 557
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

</ul>

## Prerequisites

The tutorial assumes that you have:

- installed YugabyteDB and created a universe. If not, follow the steps in [Quick start](/preview/tutorials/quick-start/).
- a 32-bit (x86) or 64-bit (x64) architecture machine.
- gcc 4.1.2 or later, clang 3.4 or later installed.

## Install the libpq C driver

The `libpq` C driver is included in the YugabyteDB installation. You can use it by setting the `LD_LIBRARY_PATH` as follows :

```sh
$ export LD_LIBRARY_PATH=<yugabyte-install-dir>/postgres/lib
```

Alternatively, you can download the PostgreSQL binaries and source from the [PostgreSQL Downloads](https://www.postgresql.org/download/) page.

## Create the sample C application

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

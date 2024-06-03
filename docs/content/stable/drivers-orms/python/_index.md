---
title: Python drivers and ORMs
headerTitle: Python
linkTitle: Python
description: Python Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  stable:
    identifier: python-drivers
    parent: drivers-orms
    weight: 520
type: indexpage
showRightNav: true
---
## Supported projects

The following projects can be used to implement Python applications using the YugabyteDB YSQL API.

| Project | Documentation and Guides | Latest Driver Version | Supported YugabyteDB Version |
| ------- | ------------------------ | ------------------------ | ---------------------|
| Yugabyte Psycopg2 Smart Driver [Recommended] | [Documentation](yugabyte-psycopg2/) <br /> [Reference](../../reference/drivers/python/yugabyte-psycopg2-reference/)| 2.9.3 | 2.8 and above |
| PostgreSQL Psycopg2 Driver | [Documentation](postgres-psycopg2/) <br /> [Reference](../../reference/drivers/python/postgres-psycopg2-reference/) | 2.9.3 | 2.8 and above |
| aiopg | [Documentation](aiopg/) | 1.4 | 2.8 and above |
| YugabyteDB Python Driver for YCQL | [Documentation](ycql/) | [3.25.0](https://github.com/yugabyte/cassandra-python-driver/tree/master) | |

| Project | Documentation and Guides | Example Apps |
| ------- | ------------------------ | ------------ |
| SQLAlchemy | [Documentation](sqlalchemy/) <br/> [Hello World](../orms/python/ysql-sqlalchemy/) | [SQLAlchemy ORM App](https://github.com/YugabyteDB-Samples/orm-examples/tree/master/python/sqlalchemy)
| Django | [Documentation](django/) <br/> [Hello World](../orms/python/ysql-django/) | [Django ORM App](https://github.com/YugabyteDB-Samples/orm-examples/tree/master/python/django) |

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations by referring to [Connect an app](yugabyte-psycopg2/) or [Use an ORM](sqlalchemy/).

For reference documentation, including using projects with SSL, refer to the [drivers and ORMs reference](../../reference/drivers/python/yugabyte-psycopg2-reference/) pages.

## Prerequisites

To develop Python applications for YugabyteDB, you need the following:

- **Python**
  Ensure your system has Python3 installed. To check the version of Python installed, use the following command:

  ```sh
  python -V
  ```

  If not already installed, download and install it from the [Python Downloads](https://www.python.org/downloads/) page.

- **YugabyteDB cluster**
  - Create a free cluster on [YugabyteDB Managed](https://www.yugabyte.com/cloud/). Refer to [Use a cloud cluster](../../quick-start-yugabytedb-managed/). Note that YugabyteDB Managed requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](../../quick-start/).

## Next step

- [Connect an app](yugabyte-psycopg2/)

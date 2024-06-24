---
title: Python drivers and ORMs
headerTitle: Python
linkTitle: Python
description: Python Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.14:
    identifier: python-drivers
    parent: drivers-orms
    weight: 570
type: indexpage
---
The following projects can be used to implement Python applications using the YugabyteDB YSQL API.

## Supported projects

| Project | Documentation and Guides | Latest Driver Version | Supported YugabyteDB Version |
| ------- | ------------------------ | ------------------------ | ---------------------|
| Yugabyte Psycopg2 Driver (Recommended) | [Documentation](yugabyte-psycopg2/) <br /> [Reference page](../../reference/drivers/python/yugabyte-psycopg2-reference/)| 2.9.3 | 2.8 and above |
| PostgreSQL Psycopg2 | [Documentation](postgres-psycopg2/) <br /> [Hello World](../../quick-start/build-apps/python/ysql-psycopg2/) <br /> [Reference page](../../reference/drivers/python/postgres-psycopg2-reference/) | 2.9.3 | 2.8 and above |

| Project | Documentation and Guides | Example Apps |
| ------- | ------------------------ | ------------ |
| SQLAlchemy | [Documentation](sqlalchemy/) | [Hello World](../../quick-start/build-apps/python/ysql-sqlalchemy/) |
| Django | [Documentation](django/) | [Hello World](../../quick-start/build-apps/python/ysql-django/) |

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations using the **Hello World** examples.

For fully-runnable code snippets and explanations of common operations, see the **example apps**. Before running the example apps, make sure you have installed the prerequisites.

For reference documentation, including using projects with SSL, refer to the [drivers and ORMs reference](../../reference/drivers/python/postgres-psycopg2-reference/) pages.

## Prerequisites

To develop Python applications for YugabyteDB, you need the following:

- **Python**\
  Ensure your system has Python3 installed. To check the version of Python installed, use the following command:

  ```sh
  python -V
  ```

  If not already installed, download and install it from the [Python Downloads](https://www.python.org/downloads/) page.

- **Create a Python project**\
  Create a python file by adding the `.py` extension to the filename. A virtual environment is also recommended to keep dependencies required by different projects separate. Make sure `pip` is also installed in the environment.

- **YugabyteDB cluster**
  - Create a free cluster on YugabyteDB Managed. Refer to [Use a cloud cluster](/preview/quick-start-yugabytedb-managed/). Note that YugabyteDB Managed requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](../../quick-start/).

## Next steps

- Learn how to build Python applications using [Django ORM](django/).
- Learn how to [use SQLAlchemy with YugabyteDB](sqlalchemy/)

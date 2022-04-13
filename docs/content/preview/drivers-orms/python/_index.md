---
title: Python drivers and ORMs
headerTitle: Python
headcontent: Prerequisites and CRUD examples for building applications in Python.
linkTitle: Python
description: Python Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    identifier: python-drivers
    parent: drivers-orms
    weight: 570
isTocNested: true
showAsideToc: true
---

The following projects can be used to implement Python applications using the YugabyteDB YSQL API.

| Project (* Recommended) | Type | Support | Examples |
| :------ | :--- | :------ | :------- |
| [Yugabyte Psycopg2*](yugabyte-psycopg2) | Python Driver | Full | [Hello World](/preview/quick-start/build-apps/python/ysql-psycopg2/) <br />[CRUD](yugabyte-psycopg2) |
| [PostgreSQL Psycopg2](postgres-psycopg2) | Python Driver | Full | [Hello World](/preview/quick-start/build-apps/python/ysql-psycopg2) <br />[CRUD](postgres-psycopg2) |
| [SQLAlchemy](sqlalchemy) | ORM |  Full | [Hello World](/preview/quick-start/build-apps/python/ysql-sqlalchemy) <br />[CRUD](sqlalchemy) |
| [Django](django) | ORM |  Full | [Hello World](/preview/quick-start/build-apps/python/ysql-django) <br />[CRUD](django) |

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations using the **Hello World** examples.

For fully-runnable code snippets and explanations of common operations, see the project page **CRUD** example. Before running CRUD examples, make sure you have installed the prerequisites.

For reference documentation, including using projects with SSL, refer to the [drivers and ORMs reference](../../reference/drivers/python/postgres-psycopg2-reference/) pages.

## Prerequisites

To develop Python applications for YugabyteDB, you need the following:

- **Python**\
  Ensure your system has Python3 installed. To check the version of Python installed, use the following command:

  ```sh
  $ python -V
  ```

  If not already installed, download and install it from the [Python Downloads](https://www.python.org/downloads/) page.

- **Create a Python project**\
  Create a python file by adding the ```.py``` extension to the filename. A virtual environment is also recommended to keep dependencies required by different projects separate. Make sure `pip` is also installed in the environment.

- **YugabyteDB cluster**
  - Create a free cluster on [Yugabyte Cloud](https://www.yugabyte.com/cloud/). Refer to [Create a free cluster](../../yugabyte-cloud/cloud-basics/create-clusters-free/). Note that Yugabyte Cloud requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](/preview/quick-start/install/macos).

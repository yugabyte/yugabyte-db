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

The following projects can be used to implement Python applications using the YugabyteDB YSQL API. For fully runnable code snippets and explanations for common operations, see the specific Java driver and ORM section. The following table provides links to driver-specific documentation and examples.

| Project (* Recommended) | Type | Support | Examples |
| :------ | :--- | :------ | :------- |
| [Yugabyte Psycopg2*](yugabyte-psycopg2) | Python Driver | Full | [Hello World](/preview/quick-start/build-apps/python/ysql-psycopg2/) <br />[CRUD App](yugabyte-psycopg2) |
| [PostgreSQL Psycopg2](postgres-psycopg2) | Python Driver | Full | [Hello World](/preview/quick-start/build-apps/python/ysql-psycopg2) <br />[CRUD App](postgres-psycopg2) |
| [SQLAlchemy](sqlalchemy) | ORM |  Full | [Hello World](/preview/quick-start/build-apps/python/ysql-sqlalchemy) <br />[CRUD App](sqlalchemy) |
| [Django](django) | ORM |  Full | [Hello World](/preview/quick-start/build-apps/python/ysql-django) <br />[CRUD App](django) |

## Build a Hello World application

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps in [Build an Application](/preview/quick-start/build-apps/python/ysql-psycopg2) in the Quick Start section.

## Prerequisites for building Python applications

### Install Python

Make sure that your system has Python3 installed. To check the version of Python installed, use the following command.

```sh
$ python -V
```

If not already installed, download and install it from [Python's Website](https://www.python.org/downloads/).

### Create a Python project

Create a python file by adding ```.py``` extension to your filename. It is also advised to start a virtual environment to keep dependencies required by different projects separate. Make sure pip is also installed in the environment.

### Create a YugabyteDB cluster

Create a free cluster on Yugabyte Cloud. Refer to [Create a free cluster](/preview/yugabyte-cloud/cloud-quickstart/qs-add/).

You can also set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](/preview/quick-start/install/macos).

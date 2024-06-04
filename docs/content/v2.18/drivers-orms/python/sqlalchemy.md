---
title: SQLAlchemy ORM
headerTitle: Use an ORM
linkTitle: Use an ORM
description: Python SQLAlchemy ORM support for YugabyteDB
menu:
  v2.18:
    identifier: sqlalchemy-orm
    parent: python-drivers
    weight: 600
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../sqlalchemy/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      SQLAlchemy ORM
    </a>
  </li>

  <li >
    <a href="../django/" class="nav-link">
      <i class="fa-brands fa-java" aria-hidden="true"></i>
      Django ORM
    </a>
  </li>

</ul>

[SQLAlchemy](https://www.sqlalchemy.org/) is a popular ORM provider for Python applications, and is widely used by Python developers for database access. YugabyteDB provides full support for SQLAlchemy ORM.

## CRUD operations

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps in [Python ORM example application](../../orms/python/ysql-sqlalchemy/) page.

The following sections demonstrate how to perform common tasks required for Python application development using the SQLAlchemy ORM.

### Add the SQLAlchemy ORM dependency

To download and install SQLAlchemy to your project, use the following command.

```shell
pip3 install sqlalchemy
```

You can verify the installation as follows:

1. Open the Python prompt by executing the following command:

    ```sh
    python3
    ```

1. From the Python prompt, execute the following commands to check the SQLAlchemy version:

    ```python
    import sqlalchemy
    ```

    ```python
    sqlalchemy.__version__
    ```

### Implement ORM mapping for YugabyteDB

To start with SQLAlchemy, in your project directory, create 4 Python files - `config.py`,`base.py`,`model.py`, and `main.py`

1. `config.py` contains the credentials to connect to your database. Copy the following sample code to the `config.py` file.

    ```python
     db_user = 'yugabyte'
     db_password = 'yugabyte'
     database = 'yugabyte'
     db_host = 'localhost'
     db_port = 5433
    ```

1. Next, declare a mapping. When using the ORM, the configuration process begins with describing the database tables you'll use, and then defining the classes which map to those tables. In modern SQLAlchemy, these two tasks are usually performed together, using a system known as **Declarative Extensions**. Classes mapped using the Declarative system are defined in terms of a base class which maintains a catalog of classes and tables relative to that base - this is known as the declarative base class. You create the base class using the `declarative_base()` function. Add the following code to the `base.py` file.

    ```python
    from sqlalchemy.ext.declarative import declarative_base
    Base = declarative_base()
    ```

1. Now that you have a _base_, you can define any number of mapped classes in terms of it. Start with a single table called `employees`, to store records for the end-users using your application. A new class called `Employee` maps to this table. In the class, you define details about the table to which you're mapping; primarily the table name, and names and datatypes of the columns. Add the following to the `model.py` file:

    ```python
    from sqlalchemy.ext.declarative import declarative_base
    from sqlalchemy import Table, Column, Integer, String, DateTime, ForeignKey
    from base import Base

    class Employee(Base):

      __tablename__ = 'employees'

      id = Column(Integer, primary_key=True)
      name = Column(String(255), unique=True, nullable=False)
      age = Column(Integer)
      language = Column(String(255))
    ```

1. After the setup is done, you can connect to the database and create a new session. In the `main.py` file, add the following:

    ```python
    import config as cfg
    from sqlalchemy.orm import sessionmaker, relationship
    from sqlalchemy import create_engine
    from sqlalchemy import MetaData
    from model import Employee
    from base import Base
    from sqlalchemy import Table, Column, Integer, String, DateTime, ForeignKey

    # create connection
    engine = create_engine('postgresql://{0}:{1}@{2}:{3}/{4}'.format(cfg.db_user, cfg.db_password, cfg.db_host, cfg.db_port, cfg.database))

    # create metadata
    Base.metadata.create_all(engine)

    # create session
    Session = sessionmaker(bind=engine)
    session = Session()

    # insert data
    tag_1 = Employee(name='Bob', age=21, language='Python')
    tag_2 = Employee(name='John', age=35, language='Java')
    tag_3 = Employee(name='Ivy', age=27, language='C++')

    session.add_all([tag_1, tag_2, tag_3])

    # Read the inserted data

    print('Query returned:')
    for instance in session.query(Employee):
        print("Name: %s Age: %s Language: %s"%(instance.name, instance.age, instance.language))
    session.commit()
    ```

When you run the `main.py` file, you should get the output similar to the following:

```text
Query returned:
Name: Bob Age: 21 Language: Python
Name: John Age: 35 Language: Java
Name: Ivy Age: 27 Language: C++
```

## Learn more

- Build Python applications using [Django ORM](../django/)
- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)

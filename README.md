[![Build Status](https://travis-ci.com/bitnine-oss/agensgraph-ext.svg?branch=master)](https://travis-ci.com/bitnine-oss/agensgraph-ext)

Apache AGE(Incubating)
==========
Note: AgensGraph-Extension was renamed to Apache AGE based on Apache requirements since we donated this project to Apache Software Foundation. 

Apache AGE is an extension of PostgreSQL that provides an implemtation of the [openCypher](https://www.opencypher.org/) query language.

This project is currently in alpha stage.

The initial goal is as follows.

* Support `CREATE` clause with a vertex
* Support `MATCH` clause with a vertex

Installation
============

Requirements
------------

Apache AGE is an extension of PostgreSQL, its build system is based on Postgres' [`Extension Building Infrastructure`](https://www.postgresql.org/docs/11/extend-pgxs.html). Therefore, **pg_config** for the PostgreSQL installation is used to build it.

Apache AGE 0.1.0 supports PostgreSQL 11.

Installation Procedure
----------------------

The build process will attempt to use the first path in the PATH environment variable when installing AGE. If the pg_config path if located there, run the following command in the source code directory of Apache AGE to build and install the extension.

    $ make install

If the path to your Postgres installation is not in the PATH variable, add the path in the arguements:

    $ make PG_CONFIG=/path/to/postgres/bin/pg_config install

Since Apache AGE will be installed in the directory of the PostgreSQL installation, proper permissions on the directory are required.

Run the following statements in ``psql`` to create and load age in PostgreSQL.

    =# CREATE EXTENSION age; -- run this statement only once
    CREATE EXTENSION
    =# LOAD 'age';
    LOAD
    =# SET search_path = ag_catalog, "$user", public;
    SET

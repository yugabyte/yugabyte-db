Installation
============

Installtaion from Source Code
-----------------------------

Requirements
~~~~~~~~~~~~

Since |project| is an extension of PostgreSQL, its build system is based on `Extension Building Infrastructure`_ that PostgreSQL provides. Therefore, ``pg_config`` (typically the first one in your ``PATH`` environment variable) for the PostgreSQL installation is used to build it.

  |project| |release| supports PostgreSQL 11.

.. _Extension Building Infrastructure: https://www.postgresql.org/docs/11/extend-pgxs.html

Getting The Source
~~~~~~~~~~~~~~~~~~

The |project| |release| sources can be obtained from its GitHub repository: `bitnine-oss/agensgraph-ext/releases`_.

.. _bitnine-oss/agensgraph-ext/releases: https://github.com/bitnine-oss/agensgraph-ext/releases

Installation Procedure
~~~~~~~~~~~~~~~~~~~~~~

To build and install |project|, run the following command in the source code directory of |project|.

.. code-block:: sh

  $ make install

..

  Since |project| will be installed in the directory of the PostgreSQL installation, proper permissions on the directory are required.

Run the following statements in ``psql`` to create and load |project| in PostgreSQL.

.. code-block:: psql

  =# CREATE EXTENSION agensgraph; -- run this statement only once
  CREATE EXTENSION
  =# LOAD 'agensgraph';
  LOAD
  =# SET search_path = ag_catalog, "$user", public;
  SET

..

  When |project| is being loaded, it installs ``post_parse_analyze_hook`` and ``set_rel_pathlist_hook`` to analyze and execute Cypher queries.

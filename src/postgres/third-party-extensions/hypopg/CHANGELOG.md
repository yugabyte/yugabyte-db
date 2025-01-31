Changelog
=========

2024-04-28 version 1.4.1:
-------------------------

  **Bug fixes**:

  - Fix hypothetical index names when its Oid is more than 1 billion (Julien
    Rouhaud, thanks to github user sylph520 for the report)
  - Fix hypothetical index deparsing when attributes need quoting (Julien
    Rouhaud, thanks to Daniel Lang for the report)

  **Miscellaneous**:

  - Add support for PostgreSQL 17 (Georgy Shelkovy, Julien Rouhaud)

2023-05-27 version 1.4.0:
-------------------------

  **New features**:

  - Support hypothetically hiding existing indexes, hypothetical or not (github
    user nutvii and Julien Rouhaud)

  **Miscellaneous**:

  - Have hypopg_relation_size() error out rather than returning 0 if called for
    an oid that isn't a hypothetical index oid
  - Slighthly reduce memory usage for hypothetical btree indexes without
    INCLUDE keys

2021-06-21 version 1.3.1:
-------------------------

  **Miscellaneous**:

  - Fix compatibility with PostgreSQL 14 beta 2

2021-06-04 version 1.3.0:
-------------------------

  **New features**:

  - Add support for hypothetical hash indexes (pg10+)

2021-02-26 version 1.2.0:
-------------------------

  **New features**:

  - Make hypopg work on standby servers using a new "fake" oid generator, that
    borrows Oids in the FirstBootstrapObjectId / FirstNormalObjectId range
    rather than real oids.  If necessary, the old behavior can still be used
    with the new hypopg.use_real_oids configuration option.

  **Bug fixes**

  - Check if access methods support an INCLUDE clause to avoid creating invalid
    hypothetical indexes.
  - Display hypothetical indexes on dropped table in hypopg_list_indexes.

  **Miscellaneous**

  - Change hypopg_list_indexes() to view hypopg_list_indexes.
  - Various documentation improvements.

2020-06-24 version 1.1.4:
-------------------------

  **New features**:

    - Add support for hypothetical index on partitioned tables

  **Miscellaneous**

  - Fix compatibility with PostgreSQL 13

  **Bug fixes**

  - Check that the target relation is a table or a materialized view

2019-06-16 version 1.1.3:
-------------------------

  **Miscellaneous**

  - Fix compatibility with PostgreSQL 12
  - Don't leak client_encoding change after hypopg extension is created
    (Michael Kröll)
  - Use a dedicated MemoryContext to store hypothetical objects
  - Fix compatibility on Windows (Godwottery)

  **Bug fixes**

  - Call previous explain_get_index_name_hook if it was setup
  - add hypopg_reset_index() SQL function

2018-05-30 version 1.1.2:
-------------------------

  **New features**

    - Add support for INCLUDE on hypothetical indexes (pg11+)
    - Add support for parallel hypothetical index scan (pg11+)

  **Bug fixes:**

    - Fix support for pg11, thanks to Christoph Berg for the report

2018-03-20 version 1.1.1:
-------------------------

  **Bug fixes**:

    - Fix potentially uninitialized variables, thanks to Jeremy Finzel for the
      report.
    - Support hypothetical indexes on materialized view, thanks to Andrew Kane
      for the report.

  **Miscellaneous**:

    - add support for PostgreSQL 11

2017-10-04 version 1.1.0:
-------------------------

  **New features**:

    - add support for hypothetical indexes on expression
    - add a hypopg_get_indexdef() function to get definition of a stored
      hypothetical index

  **Bug fixes**:

    - don't allow hypothetical unique or multi-column index if the AM doesn't
      support it
    - disallow hypothetical indexes on system columns (except OID)
    - fix indexes using DESC clause and default NULLS ordering, thanks to
      Andrew Kane for the report and test case.
    - fix PostgreSQL 9.6+ support, thanks to Rob Stolarz for the report

  **Miscellaneous**:

    - add support for PostgreSQL 10

2016-10-24 version 1.0.0:
-------------------------

  - fix memory leak in hypopg() function

2016-07-07 version 0.0.5:
-------------------------

  - add support for PostgreSQL 9.6, thanks to Konstantin Mosolov for fixing some
    issues
  - add support from new bloom access method (9.6+)
  - fix issue with hypothetical indexes on expression (thanks to Konstantin
    Mosolov)
  - fix possible crash in hypothetical index size estimation

2015-11-06 version 0.0.4:
-------------------------

  - remove the simplified "hypopg_add_index()" function
  - free memory when hypothetical index creation fails
  - check that number of column is suitable for a real index
  - for btree indexes, check that the estimated average row size is small
    enough to allow a real index creation.
  - handle BRIN indexes.
  - handle index storage parameters for supported index methods.
  - handle index on predicate.
  - safer handling of locks.

2015-08-08 version 0.0.3:
-------------------------

  - fix a bug when a regular query could fail after a hypothetical index have
  been created, and tested with explain.
  - hypopg_create_index() and hypopg_add_index() now returns the oid and index
  names.
  - add hypopg.enabled GUC. It allows disabling HypoPG globally or in a single
  backend. Thanks to Ronan Dunklau for the patch.

2015-07-08 version 0.0.2:
-------------------------

  - fix crash when building hypothetical index on expression, thanks to Thom
  Brown for the report.

2015-06-24 version 0.0.1:
-------------------------

  - First version of HypoPG.

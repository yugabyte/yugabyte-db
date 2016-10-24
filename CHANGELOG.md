Changelog
=========

2016-10-24 version 1.0.0:

  - fix memory leak in hypopg() function

2016-07-07 version 0.0.5:

  - add support for PostgreSQL 9.6, thanks to Konstantin Mosolov for fixing some
    issues
  - add support from new bloom access method (9.6+)
  - fix issue with hypothetical indexes on expression (thanks to Konstantin
    Mosolov)
  - fix possible crash in hypothetical index size estimation

2015-11-06 version 0.0.4:

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

  - fix a bug when a regular query could fail after a hypothetical index have
  been created, and tested with explain.
  - hypopg_create_index() and hypopg_add_index() now returns the oid and index
  names.
  - add hypopg.enabled GUC. It allows disabling HypoPG globally or in a single
  backend. Thanks to Ronan Dunklau for the patch.

2015-07-08 version 0.0.2:

  - fix crash when building hypothetical index on expression, thanks to Thom
  Brown for the report.

2015-06-24 version 0.0.1:

  - First version of HypoPG.

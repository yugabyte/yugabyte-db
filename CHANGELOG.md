Changelog
=========

WIP version 0.0.4:

  - handle index on predicate
  - safer handling of locks

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

  - First version of HypoPG

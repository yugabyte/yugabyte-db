2022-08-25
==========
v2.17
- Updated for PostgreSQL 15

2021-09-09
==========
v2.16
- Updated for PostgreSQL 14

2020-12-08
==========
v2.15.1
- [PR #112](https://github.com/citusdata/postgresql-hll/pull/112), Fixes tests on glibc>=2.29

2020-11-26
==========
v2.15
- Updated for PostgreSQL 13
- [PR #73](https://github.com/citusdata/postgresql-hll/pull/73), Support parallel queries / aggregates
- [PR #107](https://github.com/citusdata/postgresql-hll/pull/107), Fixed out of range bug

2019-11-15
==========
v2.14 - Fixed an issue in version upgrades

2019-11-06
==========
v2.13 - Updated for PostgreSQL 12

2018-11-03
==========
v2.12 - More updates for PostgreSQL 11 and fixes

2018-08-25
==========
v2.11 - Updated for PostgreSQL 11

2017-06-22
==========
v2.10.2 - Updated for PostgreSQL 10

2016-09-22
==========
v2.10.1 - Updated for PostgreSQL 9.4 and 9.5.

2014-01-10
==========
v2.10.0 - Merged [PR #17](https://github.com/citusdata/postgresql-hll/pull/17), cleaned up compiler warnings and test cruft, added binary IO type for hll.

2013-12-16
==========
v2.9.0 - Fixed [issue #16](https://github.com/citusdata/postgresql-hll/issues/16), `bitstream_pack` fixed to write one byte at a time to avoid writing to unallocated memory.

2013-07-16
==========
v2.8.0 - Fixed [issue #2](https://github.com/citusdata/postgresql-hll/issues/2), `hll_add_agg` now returns `hll_empty` on input of an empty set. (Thanks to @ozgune!)

2013-06-12
==========
v2.7.1 - Build fixes for OS X and Debian. Documentation fixes. Small changes to test format to improve stability across `psql` versions.

2013-02-04
==========
v2.7 - First open-source release.

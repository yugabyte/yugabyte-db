# DocumentDB RUM - RUM access method

## Introduction

The **rum** module provides an access method to work with a `RUM` index. It is based
on the `GIN` access method's code.

`documentdb_rum` is a fork of the RUM access method that extends it to allow more scenarios centered around documents.
Note that the folloiwng invariants are true:

1) From a storage standpoint, it has the same layout and content on disk as RUM (it is fully back-wards compatible)
2) All changes must be made only on the query path and volatile path or in a way that works for indexes that are built with the default RUM repo.

## License

This module is available under the [license](LICENSE) similar to
[PostgreSQL](http://www.postgresql.org/about/licence/).

## Installation

run make install on this directory after pulling in the rest of documentdb.

## Authors
See [README](https://github.com/postgrespro/rum/blob/master/README.md)
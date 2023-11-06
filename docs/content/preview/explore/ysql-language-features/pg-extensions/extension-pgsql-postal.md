---
title: pgsql-postal extension
headerTitle: pgsql-postal extension
linkTitle: pgsql-postal
description: Using the pgsql-postal extension in YugabyteDB
menu:
  preview:
    identifier: extension-pgsql-postal
    parent: pg-extensions
    weight: 20
type: docs
---

The [pgsql-postal](https://github.com/pramsey/pgsql-postal) extension parses and normalizes street addresses around the world using libpostal.

## Install

First install `libpostal` [from source](https://github.com/openvenues/libpostal) locally:

```sh
`make -j$(nproc) && sudo make install`
```

To build `pgsql-postal` against the correct PostgreSQL version for YugabyteDB compatibility, install PostgreSQL 11 on your system as described in the [PostGIS example](../extension-postgis/).

Build `pgsql-postal` [from source](https://github.com/pramsey/pgsql-postal) locally. First make sure to set `PG_CONFIG` in `Makefile` to the correct PostgreSQL version (for example, on CentOS `PG_CONFIG=/usr/pgsql-11/bin/pg_config`), then run `make`.

Copy the needed files into your YugabyteDB installation:

```sh
cp -v /usr/local/lib/libpostal.so* "$(yb_pg_config --pkglibdir)" &&
cp -v postal-1.0.sql postal.control "$(yb_pg_config --sharedir)"/extension
```

On Linux systems, run the post-install tool:

```sh
./bin/post_install.sh -e
```

Create the extension:

```sh
./bin/ysqlsh -c "CREATE EXTENSION postal"
```

## Example

Run some sample queries by connecting using `ysqlsh` and running the following:

```sql
SELECT unnest(postal_normalize('412 first ave, victoria, bc'));
```

```output
                  unnest
------------------------------------------
 412 1st avenue victoria british columbia
 412 1st avenue victoria bc
 412 1 avenue victoria british columbia
 412 1 avenue victoria bc
(4 rows)
```

```sql
SELECT postal_parse('412 first ave, victoria, bc');
```

```output
                                  postal_parse
---------------------------------------------------------------------------------
 {"city": "victoria", "road": "first ave", "state": "bc", "house_number": "412"}
(1 row)
```
